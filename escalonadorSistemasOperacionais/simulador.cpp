#include "Simulador.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

Simulador::Simulador(const ConfiguracaoSistema& config) : configOriginal_(config) {
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
            if (oc.estado == EstadoOcorrencia::Futura && instanteAtual >= oc.instanteAtivacao) {
                oc.estado = EstadoOcorrencia::Pronta;
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
    std::sort(candidatas.begin(), candidatas.end(), [&](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        const auto& t1 = proximo.tarefas[a.first];
        const auto& o1 = t1.ocorrencias[a.second];
        const auto& t2 = proximo.tarefas[b.first];
        const auto& o2 = t2.ocorrencias[b.second];

        // Critério Algoritmo (RM / EDF)
        if (configOriginal_.algoritmo == "RM") {
            if (t1.periodo != t2.periodo) return t1.periodo < t2.periodo;
        }
        else if (configOriginal_.algoritmo == "EDF") {
            if (o1.deadlineAbsoluto != o2.deadlineAbsoluto) return o1.deadlineAbsoluto < o2.deadlineAbsoluto;
        }

        // Critério: Tarefa que já estava em execução leva vantagem para evitar troca de contexto
        bool t1Exec = (o1.estado == EstadoOcorrencia::Executando);
        bool t2Exec = (o2.estado == EstadoOcorrencia::Executando);
        if (t1Exec != t2Exec) return t1Exec;

        // Desempates
        if (t1.prazo != t2.prazo) return t1.prazo < t2.prazo;
        if (o1.instanteAtivacao != o2.instanteAtivacao) return o1.instanteAtivacao < o2.instanteAtivacao;
        if (t1.duracao != t2.duracao) return t1.duracao < t2.duracao;
        return t1.id < t2.id;
        });

    // 4. Separar as N vencedoras (Limitado ao número de CPUs)
    size_t qtdSelecionadas = std::min(candidatas.size(), proximo.cpus.size());
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

        if (proximo.relogioGlobal > oSel.deadlineAbsoluto && oSel.tempoRestante > 0) {
            oSel.perdeuPrazo = true;
        }

        RegistoGantt rg;
        rg.instante = proximo.relogioGlobal;
        rg.idTarefa = tSel.id;
        rg.numeroOcorrencia = oSel.numero;
        rg.idCPU = c;
        rg.estado = EstadoOcorrencia::Executando;
        rg.eventoConclusao = (oSel.tempoRestante == 0);
        rg.eventoPerdaPrazo = oSel.perdeuPrazo;
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