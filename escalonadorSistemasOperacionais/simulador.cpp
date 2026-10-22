#include "Simulador.h"
#include "algoritmo.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <cctype>

// Cria o snapshot inicial no tick 0, prepara as dez ocorrências de cada tarefa
// periódica e inicia o gerador usado para resolver empates completos por sorteio.
Simulador::Simulador(const ConfiguracaoSistema& config)
    : configOriginal_(config),
      geradorAleatorio_(std::random_device{}()) {
    SnapshotSimulacao inicial;
    inicial.relogioGlobal = 0;
    inicial.tarefas = config.tarefas;
    inicial.cpus.resize(config.quantidadeCpus);

    for (int i = 0; i < config.quantidadeCpus; ++i) {
        inicial.cpus[i].idCPU = i;
    }

    for (auto& t : inicial.tarefas) {
        for (int k = 1; k <= 10; ++k) {
            Ocorrencia oc;
            oc.numero = k;
            oc.instanteAtivacao = t.ingresso + static_cast<long long>(k - 1) * t.periodo;
            oc.deadlineAbsoluto = oc.instanteAtivacao + t.prazo;
            oc.tempoRestante = t.duracao;
            oc.estado = EstadoOcorrencia::Futura;
            t.ocorrencias.push_back(oc);
        }
    }
    historicoSnapshots_.push_back(inicial);
}

// Devolve o snapshot selecionado pelo índice atual do histórico.
// A referência evita copiar todos os dados do sistema.
const SnapshotSimulacao& Simulador::getEstadoAtual() const {
    return historicoSnapshots_[indicePassoAtual_];
}

// Devolve a posição atual no histórico de snapshots.
size_t Simulador::getPassoAtual() const {
    return indicePassoAtual_;
}

// Percorre o histórico até o passo atual e agrupa, por CPU, os ticks
// consecutivos em que não houve tarefa executando.
std::vector<IntervaloCPUDesligada>
Simulador::getPeriodosCPUsDesligadas() const {
    std::vector<IntervaloCPUDesligada> periodos;

    for (size_t cpu = 0; cpu < configOriginal_.quantidadeCpus; ++cpu) {
        bool intervaloAberto = false;
        long long inicioIntervalo = 0;

        // O snapshot 0 é o estado inicial e não corresponde a um tick executado.
        for (size_t passo = 1; passo <= indicePassoAtual_; ++passo) {
            const auto& snapshot = historicoSnapshots_[passo];
            const long long inicioTick = snapshot.relogioGlobal - 1;

            const bool desligada =
                cpu < snapshot.cpusDesligadasNoTick.size() &&
                snapshot.cpusDesligadasNoTick[cpu];

            if (desligada && !intervaloAberto) {
                inicioIntervalo = inicioTick;
                intervaloAberto = true;
            } else if (!desligada && intervaloAberto) {
                periodos.push_back({
                    static_cast<int>(cpu),
                    inicioIntervalo,
                    inicioTick
                });
                intervaloAberto = false;
            }
        }

        // Se a CPU ainda está desligada, fecha o período no relógio atual.
        if (intervaloAberto) {
            periodos.push_back({
                static_cast<int>(cpu),
                inicioIntervalo,
                getEstadoAtual().relogioGlobal
            });
        }
    }

    return periodos;
}

// Consulta a indicação de conclusão armazenada no snapshot atual.
bool Simulador::estaConcluida() const {
    return getEstadoAtual().concluida;
}

// Avança a simulação em um tick. O método parte de uma cópia do estado atual
// para calcular o próximo estado sem alterar snapshots anteriores. Nesse tick,
// ativa ocorrências, verifica prazos, ordena as candidatas, distribui tarefas
// pelas CPUs, atualiza execução e quantum, registra eventos e salva o resultado
// no histórico. Se o usuário avançou após voltar no tempo, o futuro antigo é
// descartado antes de salvar o novo estado.
void Simulador::avancarTick() {
    if (historicoSnapshots_[indicePassoAtual_].concluida) return;

    SnapshotSimulacao proximo = historicoSnapshots_[indicePassoAtual_];

    // Guarda o relógio antes de incrementá-lo: uma ocorrência cuja ativação
    // coincide com esse instante fica pronta para executar neste tick.
    long long instanteAtual = proximo.relogioGlobal;
    proximo.relogioGlobal++;
    proximo.registosTick.clear();

    const std::vector<EstadoCPU> cpusAnteriores = proximo.cpus;

    // 1. Atualizar ativações pontualmente no momento certo
    for (auto& tarefa : proximo.tarefas) {
        for (auto& oc : tarefa.ocorrencias) {
            if (oc.estado == EstadoOcorrencia::Futura &&
                instanteAtual >= oc.instanteAtivacao) {
                oc.estado = EstadoOcorrencia::Pronta;

                RegistoGantt chegada{};
                chegada.instante = proximo.relogioGlobal;
                chegada.idTarefa = tarefa.id;
                chegada.numeroOcorrencia = oc.numero;
                chegada.idCPU = -1;
                chegada.estado = EstadoOcorrencia::Pronta;
                chegada.eventoChegada = true;

                proximo.registosTick.push_back(chegada);
            }
        }
    }

    // Detectar perda de prazo antes de executar as tarefas deste tick.
    for (auto& tarefa : proximo.tarefas) {
        for (auto& oc : tarefa.ocorrencias) {
            if (oc.estado == EstadoOcorrencia::Futura ||
                oc.estado == EstadoOcorrencia::Concluida ||
                oc.perdeuPrazo) {
                continue;
            }

            if (proximo.relogioGlobal > oc.deadlineAbsoluto) {
                oc.perdeuPrazo = true;

                RegistoGantt evento{};
                evento.instante = proximo.relogioGlobal;
                evento.idTarefa = tarefa.id;
                evento.numeroOcorrencia = oc.numero;
                evento.idCPU = oc.cpuAtual;
                evento.estado = oc.estado;
                evento.eventoPerdaPrazo = true;

                proximo.registosTick.push_back(evento);
            }
        }
    }

    // 2. Identificar ocorrências candidatas
    std::vector<std::pair<int, int>> candidatas;
    for (size_t i = 0; i < proximo.tarefas.size(); ++i) {
        for (size_t j = 0; j < proximo.tarefas[i].ocorrencias.size(); ++j) {
            const auto& oc = proximo.tarefas[i].ocorrencias[j];
            if (oc.estado == EstadoOcorrencia::Pronta || oc.estado == EstadoOcorrencia::Executando) {
                candidatas.push_back({ static_cast<int>(i), static_cast<int>(j) });
            }
        }
    }

    // Ordena todas as candidatas em conjunto antes de distribuí-las entre as CPUs.
    // Assim, a prioridade é comparada globalmente e não depende da CPU analisada.
    auto vemAntes = [&](const std::pair<int, int>& a,
                    const std::pair<int, int>& b) {
        const auto& tarefa1 = proximo.tarefas[a.first];
        const auto& ocorrencia1 =
            tarefa1.ocorrencias[a.second];
        const auto& tarefa2 = proximo.tarefas[b.first];
        const auto& ocorrencia2 =
            tarefa2.ocorrencias[b.second];

        return prioridadeAntes(
            configOriginal_.algoritmo,
            tarefa1,
            ocorrencia1,
            tarefa2,
            ocorrencia2);
    };

    std::stable_sort(candidatas.begin(), candidatas.end(), vemAntes);

    const size_t qtdSelecionadas =
        std::min(candidatas.size(), proximo.cpus.size());

    std::vector<std::pair<int, int>> sorteadas;

    for (size_t inicio = 0; inicio < candidatas.size();) {
        size_t fim = inicio + 1;

        while (fim < candidatas.size() &&
            !vemAntes(candidatas[inicio], candidatas[fim]) &&
            !vemAntes(candidatas[fim], candidatas[inicio])) {
            ++fim;
        }

        // Só sorteia quando há empate em todos os critérios anteriores.
        if (fim - inicio > 1) {
            std::shuffle(
                candidatas.begin() + inicio,
                candidatas.begin() + fim,
                geradorAleatorio_);

            // As candidatas sorteadas que obtiveram CPU receberão marcador no Gantt.
            const size_t fimSelecionado = std::min(fim, qtdSelecionadas);
            for (size_t i = inicio; i < fimSelecionado; ++i) {
                sorteadas.push_back(candidatas[i]);
            }
        }

        inicio = fim;
    }

    // 4. Separar as N vencedoras (Limitado ao número de CPUs)
    std::vector<std::pair<int, int>> selecionadas(candidatas.begin(), candidatas.begin() + qtdSelecionadas);
    std::vector<std::pair<int, int>> emEspera(candidatas.begin() + qtdSelecionadas, candidatas.end());

    // Mantém na mesma CPU as ocorrências que continuam executando e ainda foram
    // selecionadas. Depois, distribui as demais pelas CPUs livres, evitando migrações
    // desnecessárias sem permitir que uma CPU receba duas ocorrências no mesmo tick.
    std::vector<bool> cpuLivre(proximo.cpus.size(), true);
    std::vector<int> alocacaoCPU(selecionadas.size(), -1);

    for (size_t i = 0; i < selecionadas.size(); ++i) { // Fase 1: Manter na CPU antiga
        const auto& oSel = proximo.tarefas[selecionadas[i].first].ocorrencias[selecionadas[i].second];
        if (oSel.estado == EstadoOcorrencia::Executando && oSel.cpuAtual != -1 && cpuLivre[oSel.cpuAtual]) {
            alocacaoCPU[i] = oSel.cpuAtual;
            cpuLivre[oSel.cpuAtual] = false;
        }
    }
    for (size_t i = 0; i < selecionadas.size(); ++i) { // Fase 2: Alocar nas CPUs livres restantes
        if (alocacaoCPU[i] == -1) {
            for (size_t c = 0; c < cpuLivre.size(); ++c) {
                if (cpuLivre[c]) {
                    alocacaoCPU[i] = static_cast<int>(c);
                    cpuLivre[c] = false;
                    break;
                }
            }
        }
    }

    // 6. Executar e registar eventos das tarefas escalonadas
    for (size_t i = 0; i < selecionadas.size(); ++i) {
        int c = alocacaoCPU[i];
        auto& tSel = proximo.tarefas[selecionadas[i].first];
        auto& oSel = tSel.ocorrencias[selecionadas[i].second];
        auto& cpu = proximo.cpus[c];
        const auto& cpuAnterior = cpusAnteriores[c];

        bool mesmaOcorrencia = (cpuAnterior.idTarefaAtual == tSel.id && cpuAnterior.numeroOcorrenciaAtual == oSel.numero);
        int ticksNaFatia = mesmaOcorrencia ? cpuAnterior.tempoExecutadoNoQuantum + 1 : 1;
        bool fimQuantum = (ticksNaFatia >= configOriginal_.quantum);

        const bool jaEstavaExecutando =
        oSel.estado == EstadoOcorrencia::Executando;

        oSel.estado = EstadoOcorrencia::Executando;
        oSel.cpuAtual = c;
        oSel.tempoRestante--;

        RegistoGantt rg;
        rg.instante = proximo.relogioGlobal;
        rg.idTarefa = tSel.id;
        rg.numeroOcorrencia = oSel.numero;
        rg.idCPU = c;
        rg.estado = EstadoOcorrencia::Executando;
        rg.eventoConclusao = (oSel.tempoRestante == 0);
        rg.eventoSorteio =
            !jaEstavaExecutando &&
            std::find(sorteadas.begin(), sorteadas.end(), selecionadas[i])
            != sorteadas.end();
        rg.eventoFimQuantum = fimQuantum && oSel.tempoRestante > 0;
        proximo.registosTick.push_back(rg);

        // Libera ou mantém a CPU conforme o resultado da execução deste tick:
        // a ocorrência pode ter concluído, esgotado o quantum ou continuar executando.
        if (oSel.tempoRestante == 0) {
            oSel.estado = EstadoOcorrencia::Concluida;
            oSel.cpuAtual = -1;
            cpu.idTarefaAtual = -1;
            cpu.numeroOcorrenciaAtual = -1;
            cpu.tempoExecutadoNoQuantum = 0;
        }
        else if (fimQuantum) {
            oSel.estado = EstadoOcorrencia::Pronta;
            oSel.cpuAtual = -1;
            cpu.idTarefaAtual = -1; // Remove a tarefa totalmente da CPU para não burlar o limite
            cpu.numeroOcorrenciaAtual = -1;
            cpu.tempoExecutadoNoQuantum = 0;
        }
        else {
            cpu.idTarefaAtual = tSel.id;
            cpu.numeroOcorrenciaAtual = oSel.numero;
            cpu.tempoExecutadoNoQuantum = ticksNaFatia;
        }
    }

    // Limpar estado de CPUs não utilizadas (Ociosas)
    for (size_t c = 0; c < proximo.cpus.size(); ++c) {
        if (cpuLivre[c]) {
            proximo.cpus[c].idTarefaAtual = -1;
            proximo.cpus[c].numeroOcorrenciaAtual = -1;
            proximo.cpus[c].tempoExecutadoNoQuantum = 0;
        }
    }

    // cpuLivre[c] continua true se nenhuma tarefa foi atribuída à CPU neste tick.
    proximo.cpusDesligadasNoTick.resize(proximo.cpus.size());

    for (size_t c = 0; c < proximo.cpus.size(); ++c) {
        proximo.cpusDesligadasNoTick[c] = cpuLivre[c];
    }

    // 7. Registar tarefas que ficaram em estado de Espera/Prontas
    for (const auto& esp : emEspera) {
        auto& tEsp = proximo.tarefas[esp.first];
        auto& oEsp = tEsp.ocorrencias[esp.second];
        oEsp.estado = EstadoOcorrencia::Pronta;
        oEsp.cpuAtual = -1;

        RegistoGantt rg;
        rg.instante = proximo.relogioGlobal;
        rg.idTarefa = tEsp.id;
        rg.numeroOcorrencia = oEsp.numero;
        rg.idCPU = -1;
        rg.estado = EstadoOcorrencia::Pronta;
        proximo.registosTick.push_back(rg);
    }

    proximo.concluida = verificarConclusaoGeral(proximo);

    if (indicePassoAtual_ + 1 < historicoSnapshots_.size()) {
        historicoSnapshots_.erase(historicoSnapshots_.begin() + indicePassoAtual_ + 1, historicoSnapshots_.end());
    }

    historicoSnapshots_.push_back(proximo);
    indicePassoAtual_++;
}

// Volta o índice para o snapshot anterior sem recalcular a simulação.
// Retorna false quando já está no snapshot inicial, que representa o tick 0.
bool Simulador::retrocederTick() {
    if (indicePassoAtual_ > 0) {
        indicePassoAtual_--;
        return true;
    }
    return false;
}

// Altera manualmente o estado de uma ocorrência após validar a tarefa, o número
// da ocorrência, o instante de ativação e a disponibilidade de CPU. A função
// também mantém os dados da ocorrência e da CPU sincronizados.
void Simulador::alterarEstadoOcorrencia(
    int idTarefa,
    int numeroOcorrencia,
    EstadoOcorrencia novoEstado) {

    SnapshotSimulacao& estado = historicoSnapshots_[indicePassoAtual_];

    auto tarefa = std::find_if(
        estado.tarefas.begin(),
        estado.tarefas.end(),
        [idTarefa](const Tarefa& t) {
            return t.id == idTarefa;
        });
    
    if (tarefa == estado.tarefas.end()) {
        throw std::runtime_error("Nao existe tarefa com esse ID.");
    }

    auto ocorrencia = std::find_if(
        tarefa->ocorrencias.begin(),
        tarefa->ocorrencias.end(),
        [numeroOcorrencia](const Ocorrencia& oc) {
            return oc.numero == numeroOcorrencia;
        });

    if (ocorrencia == tarefa->ocorrencias.end()) {
        throw std::runtime_error(
            "Essa tarefa nao possui uma ocorrencia com esse numero.");
    }

    // Evita estados incompatíveis com o relógio: uma ocorrência só pode voltar
    // a Futura antes da ativação, e os demais estados só podem ser escolhidos
    // depois que ela já foi ativada.
    if (novoEstado == EstadoOcorrencia::Futura) {
        if (estado.relogioGlobal >= ocorrencia->instanteAtivacao) {
            throw std::runtime_error(
                "So e possivel voltar ao estado Futura antes da ativacao.");
        }
    } else if (estado.relogioGlobal < ocorrencia->instanteAtivacao) {
        throw std::runtime_error(
            "A ocorrencia ainda nao chegou ao instante de ativacao.");
    }

    if ((novoEstado == EstadoOcorrencia::Pronta ||
         novoEstado == EstadoOcorrencia::Suspensa ||
         novoEstado == EstadoOcorrencia::Executando) &&
        ocorrencia->tempoRestante <= 0) {
        throw std::runtime_error(
            "A ocorrencia nao tem tempo restante para esse estado.");
    }

    // Para marcar a ocorrência como Executando, é necessário reservar uma CPU.
    // Se ela já executava, tenta manter a CPU atual; caso contrário, procura uma CPU livre.
    int cpuDestino = -1;

    if (novoEstado == EstadoOcorrencia::Executando) {
        if (ocorrencia->estado == EstadoOcorrencia::Executando &&
            ocorrencia->cpuAtual >= 0 &&
            ocorrencia->cpuAtual < static_cast<int>(estado.cpus.size())) {
            cpuDestino = ocorrencia->cpuAtual;
        } else {
            for (size_t c = 0; c < estado.cpus.size(); ++c) {
                if (estado.cpus[c].idTarefaAtual == -1) {
                    cpuDestino = static_cast<int>(c);
                    break;
                }
            }
        }

        if (cpuDestino == -1) {
            throw std::runtime_error(
                "Nao ha uma CPU livre para executar essa ocorrencia.");
        }
    }

    // Remove a associação anterior antes de aplicar o novo estado, evitando que
    // a CPU continue apontando para uma ocorrência que deixou de executá-la.
    for (auto& cpu : estado.cpus) {
        if (cpu.idTarefaAtual == idTarefa &&
            cpu.numeroOcorrenciaAtual == numeroOcorrencia) {
            cpu.idTarefaAtual = -1;
            cpu.numeroOcorrenciaAtual = -1;
            cpu.tempoExecutadoNoQuantum = 0;
        }
    }

    ocorrencia->cpuAtual = -1;
    ocorrencia->estado = novoEstado;

    if (novoEstado == EstadoOcorrencia::Futura) {
        ocorrencia->tempoRestante = tarefa->duracao;
        ocorrencia->perdeuPrazo = false;
    } else if (novoEstado == EstadoOcorrencia::Concluida) {
        ocorrencia->tempoRestante = 0;
    } else if (novoEstado == EstadoOcorrencia::Executando) {
        ocorrencia->cpuAtual = cpuDestino;

        estado.cpus[cpuDestino].idTarefaAtual = idTarefa;
        estado.cpus[cpuDestino].numeroOcorrenciaAtual = numeroOcorrencia;
        estado.cpus[cpuDestino].tempoExecutadoNoQuantum = 0;
    }

    // Se o usuário voltou no histórico antes de editar, os snapshots posteriores
    // deixam de ser válidos: foram calculados a partir do estado anterior à edição.
    if (indicePassoAtual_ + 1 < historicoSnapshots_.size()) {
        historicoSnapshots_.erase(
            historicoSnapshots_.begin() + indicePassoAtual_ + 1,
            historicoSnapshots_.end());
    }

    estado.concluida = verificarConclusaoGeral(estado);
}

// Altera uma característica da tarefa no snapshot atual e valida o novo valor.
// As ocorrências que já deixaram de ser Futura mantêm seu histórico; os dados
// das ocorrências futuras são recalculados conforme a característica alterada.
void Simulador::alterarCaracteristicaTarefa(
    int idTarefa,
    const std::string& campo,
    const std::string& valor) {

    SnapshotSimulacao& estado = historicoSnapshots_[indicePassoAtual_];

    auto tarefa = std::find_if(
        estado.tarefas.begin(),
        estado.tarefas.end(),
        [idTarefa](const Tarefa& t) {
            return t.id == idTarefa;
        });

    if (tarefa == estado.tarefas.end()) {
        throw std::runtime_error("Nao existe tarefa com esse ID.");
    }

        // A cor é validada como seis dígitos hexadecimais; as outras características
        // aceitas são números inteiros sujeitos às regras de ingresso, duração, período e prazo.
        if (campo == "cor") {
        const bool corValida =
            valor.size() == 6 &&
            std::all_of(
                valor.begin(),
                valor.end(),
                [](unsigned char c) {
                    return std::isxdigit(c) != 0;
                });

        if (!corValida) {
            throw std::runtime_error(
                "A cor deve ter 6 digitos hexadecimais, por exemplo FF0000.");
        }

        tarefa->cor = valor;
    } else {
        int numero;

        try {
            size_t lidos = 0;
            numero = std::stoi(valor, &lidos);

            if (lidos != valor.size()) {
                throw std::runtime_error("");
            }
        } catch (...) {
            throw std::runtime_error(
                "O valor da caracteristica deve ser um numero inteiro.");
        }

        if (campo != "ingresso" &&
            campo != "duracao" &&
            campo != "periodo" &&
            campo != "prazo") {
            throw std::runtime_error(
                "Campo invalido. Use cor, ingresso, duracao, periodo ou prazo.");
        }

        if (campo == "ingresso" && numero < 0) {
            throw std::runtime_error("O ingresso nao pode ser negativo.");
        }
        if (campo == "duracao" && numero <= 0) {
            throw std::runtime_error("A duracao deve ser maior que zero.");
        }
        if (campo == "periodo" && numero <= 0) {
            throw std::runtime_error(
                "No Projeto A, o periodo deve ser maior que zero.");
        }
        if (campo == "prazo" && numero <= 0) {
            throw std::runtime_error("O prazo deve ser maior que zero.");
        }

        if (campo == "ingresso") {
            tarefa->ingresso = numero;
        } else if (campo == "duracao") {
            tarefa->duracao = numero;
        } else if (campo == "periodo") {
            tarefa->periodo = numero;
        } else if (campo == "prazo") {
            tarefa->prazo = numero;
        }
        
        // Ingresso e período alteram o calendário das ocorrências futuras. O cálculo
        // começa na próxima ativação possível, nunca antes do relógio atual.
        if (campo == "ingresso" || campo == "periodo") {
            long long proximaAtivacao = tarefa->ingresso;

            if (campo == "periodo") {
                for (const auto& oc : tarefa->ocorrencias) {
                    if (oc.estado != EstadoOcorrencia::Futura) {
                        proximaAtivacao =
                            oc.instanteAtivacao + tarefa->periodo;
                    }
                }
            }

            proximaAtivacao =
                std::max(proximaAtivacao, estado.relogioGlobal);
            
            // Recalcula apenas ocorrências futuras, preservando as que já foram ativadas.
            // Para cada uma, atualiza ativação, deadline e tempo restante.
            for (auto& oc : tarefa->ocorrencias) {
                if (oc.estado == EstadoOcorrencia::Futura) {
                    oc.instanteAtivacao = proximaAtivacao;
                    oc.deadlineAbsoluto =
                        oc.instanteAtivacao + tarefa->prazo;
                    oc.tempoRestante = tarefa->duracao;
                    oc.perdeuPrazo = false;
                    oc.cpuAtual = -1;

                    proximaAtivacao += tarefa->periodo;
                }
            }
        } 
        else {
            // Mudanças de duração ou prazo atualizam somente ocorrências futuras:
            // as ocorrências já ativadas mantêm os valores do histórico.
            for (auto& oc : tarefa->ocorrencias) {
                if (oc.estado != EstadoOcorrencia::Futura) {
                    continue;
                }

                if (campo == "duracao") {
                    oc.tempoRestante = tarefa->duracao;
                } else if (campo == "prazo") {
                    oc.deadlineAbsoluto =
                        oc.instanteAtivacao + tarefa->prazo;
                }
            }
        }
    }

    // Se a edição foi feita após retroceder, remove os snapshots posteriores,
    // pois eles foram calculados antes desta nova característica.
    if (indicePassoAtual_ + 1 < historicoSnapshots_.size()) {
        historicoSnapshots_.erase(
            historicoSnapshots_.begin() + indicePassoAtual_ + 1,
            historicoSnapshots_.end());
    }

    estado.concluida = verificarConclusaoGeral(estado);
}

// Considera a simulação concluída somente quando todas as ocorrências
// de todas as tarefas estão no estado Concluída.
bool Simulador::verificarConclusaoGeral(const SnapshotSimulacao& estado) const {
    for (const auto& t : estado.tarefas) {
        for (const auto& oc : t.ocorrencias) {
            if (oc.estado != EstadoOcorrencia::Concluida) {
                return false;
            }
        }
    }
    return true;
}

// Executa a simulação sem interação: avança um tick por vez até que
// verificarConclusaoGeral indique que todas as ocorrências terminaram.
void Simulador::executarSimulacaoCompleta() {
    while (!estaConcluida()) {
        avancarTick();
    }
}