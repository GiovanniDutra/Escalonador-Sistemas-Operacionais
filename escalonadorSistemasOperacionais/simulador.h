#pragma once

#include "Modelos.h"
#include <vector>
#include <string>
#include <memory>
#include <random>

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
    bool eventoSorteio = false;
};

// Estrutura que guarda o estado global do sistema num determinado tick (Snapshot)
struct SnapshotSimulacao {
    long long relogioGlobal = 0;
    std::vector<EstadoCPU> cpus;

    // Para cada CPU, indica se ela ficou desligada durante o tick que terminou.
    std::vector<bool> cpusDesligadasNoTick;

    std::vector<Tarefa> tarefas;
    std::vector<RegistoGantt> registosTick;
    bool concluida = false;
};

struct IntervaloCPUDesligada {
    int idCPU;
    long long inicio;
    long long fim;
};

class Simulador {
public:
    explicit Simulador(const ConfiguracaoSistema& config);

    // Controlo de simulação (Requisitos 1.1 e 1.5)
    void avancarTick();
    bool retrocederTick();
    void alterarEstadoOcorrencia(
        int idTarefa,
        int numeroOcorrencia,
        EstadoOcorrencia novoEstado);
    void alterarCaracteristicaTarefa(
        int idTarefa,
        const std::string& campo,
        const std::string& valor);
    void executarSimulacaoCompleta();

    // Consultas para o modo Debugger / Passo-a-Passo
    const SnapshotSimulacao& getEstadoAtual() const;
    size_t getPassoAtual() const;
    std::vector<IntervaloCPUDesligada> getPeriodosCPUsDesligadas() const;
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

    std::mt19937 geradorAleatorio_;
};