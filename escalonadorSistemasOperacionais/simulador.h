#pragma once

#include "Modelos.h"
#include <vector>
#include <string>
#include <memory>

// Registo do estado individual de cada CPU no instante atual
struct EstadoCPU {
    int idCPU = 0;
    int idTarefaAtual = -1;            // -1 se o processador estiver ocioso/desligado
    int numeroOcorrenciaAtual = -1;
    int tempoExecutadoNoQuantum = 0;
};

// Registo de evento para a construção do gráfico de Gantt
struct RegistoGantt {
    long long instante = 0;
    int idTarefa = 0;
    int numeroOcorrencia = 0;
    int idCPU = -1;                    // -1 indica fila de espera/pronta
    EstadoOcorrencia estado = EstadoOcorrencia::Futura;
    bool eventoChegada = false;
    bool eventoConclusao = false;
    bool eventoPerdaPrazo = false;
    bool eventoFimQuantum = false;
};

// Estrutura que guarda o estado global do sistema num determinado tick (Snapshot)
struct SnapshotSimulacao {
    long long relogioGlobal = 0;
    std::vector<EstadoCPU> cpus;
    std::vector<Tarefa> tarefas;
    std::vector<RegistoGantt> registosTick;
    bool concluida = false;
};

class Simulador {
public:
    explicit Simulador(const ConfiguracaoSistema& config);

    // Controlo de simulação (Requisitos 1.1 e 1.5)
    void avancarTick();
    bool retrocederTick();
    void executarSimulacaoCompleta();

    // Consultas para o modo Debugger / Passo-a-Passo
    const SnapshotSimulacao& getEstadoAtual() const;
    size_t getPassoAtual() const;
    bool estaConcluida() const;

    // Exportação do gráfico de Gantt para imagem SVG (Requisito 2.4)
    void exportarGanttSVG(const std::string& caminhoFicheiro) const;

private:
    ConfiguracaoSistema configOriginal_;
    std::vector<SnapshotSimulacao> historicoSnapshots_;
    size_t indicePassoAtual_ = 0;

    void processarAtivacoes(SnapshotSimulacao& estado);
    int selecionarTarefaParaCPU(const SnapshotSimulacao& estado, int idCPU, const std::vector<std::pair<int, int>>& candidatas);
    bool testarDesempate(const Tarefa& t1, const Ocorrencia& o1, const Tarefa& t2, const Ocorrencia& o2, int idCPUAtual);
    bool verificarConclusaoGeral(const SnapshotSimulacao& estado) const;
};