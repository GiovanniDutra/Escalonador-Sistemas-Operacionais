#include "Simulador.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <cctype>

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

const SnapshotSimulacao& Simulador::getEstadoAtual() const {
    return historicoSnapshots_[indicePassoAtual_];
}

size_t Simulador::getPassoAtual() const {
    return indicePassoAtual_;
}

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

bool Simulador::estaConcluida() const {
    return getEstadoAtual().concluida;
}

// Mantido apenas para retrocompatibilidade com o Simulador.h
void Simulador::processarAtivacoes(SnapshotSimulacao& estado) {
    // A ativação foi movida diretamente para dentro do avancarTick 
    // para garantir o tempo exato (instanteAtual).
}

bool Simulador::testarDesempate(const Tarefa& t1, const Ocorrencia& o1, const Tarefa& t2, const Ocorrencia& o2, int idCPUAtual) {
    return false; // A lógica de desempate passou para a função lambda de ordenação global
}

void Simulador::avancarTick() {
    if (historicoSnapshots_[indicePassoAtual_].concluida) return;

    SnapshotSimulacao proximo = historicoSnapshots_[indicePassoAtual_];

    // CORREÇÃO 1: Usar o instante de tempo *antes* do tick para as ativações
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

    // 3. Ordenação Global Absoluta (Resolve o problema das tarefas roubarem a CPU 0)
    auto vemAntes = [&](const std::pair<int, int>& a,
                        const std::pair<int, int>& b) {
        const auto& t1 = proximo.tarefas[a.first];
        const auto& o1 = t1.ocorrencias[a.second];
        const auto& t2 = proximo.tarefas[b.first];
        const auto& o2 = t2.ocorrencias[b.second];

        // Critério principal do algoritmo.
        if (configOriginal_.algoritmo == "RM" &&
            t1.periodo != t2.periodo) {
            return t1.periodo < t2.periodo;
        }

        if (configOriginal_.algoritmo == "EDF" &&
            o1.deadlineAbsoluto != o2.deadlineAbsoluto) {
            return o1.deadlineAbsoluto < o2.deadlineAbsoluto;
        }

        // Em empate no algoritmo, manter a tarefa que já estava executando.
        const bool t1Executava = o1.estado == EstadoOcorrencia::Executando;
        const bool t2Executava = o2.estado == EstadoOcorrencia::Executando;

        if (t1Executava != t2Executava) {
            return t1Executava;
        }

        // Desempates exigidos pelo enunciado.
        if (t1.prazo != t2.prazo) {
            return t1.prazo < t2.prazo;
        }

        if (o1.instanteAtivacao != o2.instanteAtivacao) {
            return o1.instanteAtivacao < o2.instanteAtivacao;
        }

        if (t1.duracao != t2.duracao) {
            return t1.duracao < t2.duracao;
        }

        // Retorna falso nos dois sentidos quando resta um empate completo.
        // Esse empate será resolvido por sorteio depois da ordenação.
        return false;
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

    // 5. Atribuir às CPUs mantendo a localidade (Evita pular de CPU 1 para CPU 0 atoa)
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
            std::find(sorteadas.begin(), sorteadas.end(), selecionadas[i])
            != sorteadas.end();
        rg.eventoFimQuantum = fimQuantum && oSel.tempoRestante > 0;
        proximo.registosTick.push_back(rg);

        // CORREÇÃO 2: Limpeza rigorosa da memória da CPU no Fim do Quantum
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

bool Simulador::retrocederTick() {
    if (indicePassoAtual_ > 0) {
        indicePassoAtual_--;
        return true;
    }
    return false;
}

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

    // Libera qualquer CPU que estivesse associada a esta ocorrência.
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

    // Uma alteração no passado cria uma nova linha do tempo.
    if (indicePassoAtual_ + 1 < historicoSnapshots_.size()) {
        historicoSnapshots_.erase(
            historicoSnapshots_.begin() + indicePassoAtual_ + 1,
            historicoSnapshots_.end());
    }

    estado.concluida = verificarConclusaoGeral(estado);
}

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
        // Alterações de tempo são seguras somente antes de qualquer
        // ocorrência da tarefa ter sido ativada.
        for (const auto& oc : tarefa->ocorrencias) {
            if (oc.estado != EstadoOcorrencia::Futura) {
                throw std::runtime_error(
                    "Essa caracteristica temporal nao pode ser alterada: "
                    "a tarefa ja possui ocorrencia ativada.");
            }
        }

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

        if (campo == "ingresso") {
            if (numero < 0) {
                throw std::runtime_error("O ingresso nao pode ser negativo.");
            }
            tarefa->ingresso = numero;
        } else if (campo == "duracao") {
            if (numero <= 0) {
                throw std::runtime_error("A duracao deve ser maior que zero.");
            }
            tarefa->duracao = numero;
        } else if (campo == "periodo") {
            if (numero <= 0) {
                throw std::runtime_error(
                    "No Projeto A, o periodo deve ser maior que zero.");
            }
            tarefa->periodo = numero;
        } else if (campo == "prazo") {
            if (numero <= 0) {
                throw std::runtime_error("O prazo deve ser maior que zero.");
            }
            tarefa->prazo = numero;
        } else {
            throw std::runtime_error(
                "Campo invalido. Use cor, ingresso, duracao, periodo ou prazo.");
        }

        for (auto& oc : tarefa->ocorrencias) {
            oc.instanteAtivacao =
                tarefa->ingresso +
                static_cast<long long>(oc.numero - 1) * tarefa->periodo;

            if (oc.instanteAtivacao < estado.relogioGlobal) {
                throw std::runtime_error(
                    "A nova configuracao colocaria uma ativacao no passado.");
            }

            oc.deadlineAbsoluto =
                oc.instanteAtivacao + tarefa->prazo;
            oc.tempoRestante = tarefa->duracao;
            oc.perdeuPrazo = false;
            oc.cpuAtual = -1;
        }
    }

    // A edição cria um novo ramo se o usuário havia retrocedido.
    if (indicePassoAtual_ + 1 < historicoSnapshots_.size()) {
        historicoSnapshots_.erase(
            historicoSnapshots_.begin() + indicePassoAtual_ + 1,
            historicoSnapshots_.end());
    }

    estado.concluida = verificarConclusaoGeral(estado);
}

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

void Simulador::executarSimulacaoCompleta() {
    while (!estaConcluida()) {
        avancarTick();
    }
}