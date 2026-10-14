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

    // Inicializa as 10 ocorrências padrão para cada tarefa periódica (Requisito 4.4)
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

void Simulador::processarAtivacoes(SnapshotSimulacao& estado) {
    for (auto& tarefa : estado.tarefas) {
        for (auto& oc : tarefa.ocorrencias) {
            if (oc.estado == EstadoOcorrencia::Futura && estado.relogioGlobal > oc.instanteAtivacao) {
                oc.estado = EstadoOcorrencia::Pronta;
            }
        }
    }
}

// Critérios de desempate (Requisito 4.3)
bool Simulador::testarDesempate(const Tarefa& t1, const Ocorrencia& o1, const Tarefa& t2, const Ocorrencia& o2, int idCPUAtual) {
    // 1. Tarefa que já está a executar na CPU atual (evita troca de contexto desnecessária)
    bool t1NaCPU = (o1.cpuAtual == idCPUAtual);
    bool t2NaCPU = (o2.cpuAtual == idCPUAtual);
    if (t1NaCPU != t2NaCPU) return t1NaCPU;

    // Prioridade do Algoritmo
    if (configOriginal_.algoritmo == "RM") {
        if (t1.periodo != t2.periodo) return t1.periodo < t2.periodo; // Menor período = maior prioridade
    } else if (configOriginal_.algoritmo == "EDF") {
        if (o1.deadlineAbsoluto != o2.deadlineAbsoluto) return o1.deadlineAbsoluto < o2.deadlineAbsoluto; // Menor deadline absoluto
    }

    // 2. Prazo relativo menor
    if (t1.prazo != t2.prazo) return t1.prazo < t2.prazo;

    // 3. Instante de ingresso (quem chegou primeiro)
    if (o1.instanteAtivacao != o2.instanteAtivacao) return o1.instanteAtivacao < o2.instanteAtivacao;

    // 4. Duração menor
    if (t1.duracao != t2.duracao) return t1.duracao < t2.duracao;

    // 5. Desempate por ID
    return t1.id < t2.id;
}

void Simulador::avancarTick() {
    if (historicoSnapshots_[indicePassoAtual_].concluida) return;

    // Se estivermos no meio do histórico e avançarmos, gera um novo ramo de estado
    SnapshotSimulacao proximo = historicoSnapshots_[indicePassoAtual_];
    proximo.relogioGlobal++;
    proximo.registosTick.clear();

    const std::vector<EstadoCPU> cpusAnteriores = proximo.cpus;

    // 1. Atualizar ativizações de novas ocorrências
    processarAtivacoes(proximo);

    // 2. Identificar ocorrências candidatas a execução (Prontas ou Executando)
    std::vector<std::pair<int, int>> candidatas; // pares (índiceTarefa, índiceOcorrencia)
    for (size_t i = 0; i < proximo.tarefas.size(); ++i) {
        for (size_t j = 0; j < proximo.tarefas[i].ocorrencias.size(); ++j) {
            const auto& oc = proximo.tarefas[i].ocorrencias[j];
            if (oc.estado == EstadoOcorrencia::Pronta || oc.estado == EstadoOcorrencia::Executando) {
                candidatas.push_back({static_cast<int>(i), static_cast<int>(j)});
            }
        }
    }

    // 3. Libertar CPUs cujas tarefas terminaram ou sofreram preempção
    std::vector<bool> cpuLivre(proximo.cpus.size(), true);
    for (auto& cpu : proximo.cpus) {
        cpu.idTarefaAtual = -1;
        cpu.numeroOcorrenciaAtual = -1;
        cpu.tempoExecutadoNoQuantum = 0;
    }

    // 4. Atribuir tarefas às CPUs disponíveis (Requisito 1.2)
    std::vector<bool> atribuida(candidatas.size(), false);

    for (size_t c = 0; c < proximo.cpus.size(); ++c) {
        int melhorIndice = -1;

        for (size_t k = 0; k < candidatas.size(); ++k) {
            if (atribuida[k]) continue;

            if (melhorIndice == -1) {
                melhorIndice = static_cast<int>(k);
            } else {
                const auto& t1 = proximo.tarefas[candidatas[k].first];
                const auto& o1 = t1.ocorrencias[candidatas[k].second];
                const auto& t2 = proximo.tarefas[candidatas[melhorIndice].first];
                const auto& o2 = t2.ocorrencias[candidatas[melhorIndice].second];

                if (testarDesempate(t1, o1, t2, o2, static_cast<int>(c))) {
                    melhorIndice = static_cast<int>(k);
                }
            }
        }

        if (melhorIndice != -1) {
            atribuida[melhorIndice] = true;
            auto& tSel = proximo.tarefas[candidatas[melhorIndice].first];
            auto& oSel = tSel.ocorrencias[candidatas[melhorIndice].second];

            auto& cpu = proximo.cpus[c];
            const auto& cpuAnterior = cpusAnteriores[c];

            const bool mesmaOcorrencia =
                cpuAnterior.idTarefaAtual == tSel.id &&
                cpuAnterior.numeroOcorrenciaAtual == oSel.numero;

            const int ticksNaFatia = mesmaOcorrencia
                ? cpuAnterior.tempoExecutadoNoQuantum + 1
                : 1;

            const bool fimQuantum = ticksNaFatia >= configOriginal_.quantum;

            cpu.idTarefaAtual = tSel.id;
            cpu.numeroOcorrenciaAtual = oSel.numero;
            cpu.tempoExecutadoNoQuantum = fimQuantum ? 0 : ticksNaFatia;

            oSel.estado = EstadoOcorrencia::Executando;
            oSel.cpuAtual = static_cast<int>(c);
            oSel.tempoRestante--;

            if (proximo.relogioGlobal > oSel.deadlineAbsoluto &&
                oSel.tempoRestante > 0) {
                oSel.perdeuPrazo = true;
            }

            RegistoGantt rg;
            rg.instante = proximo.relogioGlobal;
            rg.idTarefa = tSel.id;
            rg.numeroOcorrencia = oSel.numero;
            rg.idCPU = static_cast<int>(c);
            rg.estado = EstadoOcorrencia::Executando;
            rg.eventoConclusao = (oSel.tempoRestante == 0);
            rg.eventoPerdaPrazo = oSel.perdeuPrazo;
            rg.eventoFimQuantum = fimQuantum && oSel.tempoRestante > 0;
            proximo.registosTick.push_back(rg);

            if (oSel.tempoRestante == 0) {
                oSel.estado = EstadoOcorrencia::Concluida;
                oSel.cpuAtual = -1;
                cpu.idTarefaAtual = -1;
                cpu.numeroOcorrenciaAtual = -1;
                cpu.tempoExecutadoNoQuantum = 0;
            } else if (fimQuantum) {
                oSel.estado = EstadoOcorrencia::Pronta;
                oSel.cpuAtual = -1;
                cpu.idTarefaAtual = -1;
                cpu.numeroOcorrenciaAtual = -1;
            }
        }
    }

    // Regista ocorrências prontas que ficaram à espera (sem CPU)
    for (size_t k = 0; k < candidatas.size(); ++k) {
        if (!atribuida[k]) {
            auto& tEsp = proximo.tarefas[candidatas[k].first];
            auto& oEsp = tEsp.ocorrencias[candidatas[k].second];

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
    }

    proximo.concluida = verificarConclusaoGeral(proximo);

    // Se estivermos no histórico passado, truncamos o futuro e inserimos a nova ramificação
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