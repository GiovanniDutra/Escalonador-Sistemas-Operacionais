#pragma once

#include "Modelos.h"
#include <vector>
#include <string>
#include <memory>
#include <random>

// Estado de uma CPU em um instante da simulação.
// O valor -1 em idTarefaAtual e numeroOcorrenciaAtual indica que não há
// uma tarefa associada à CPU naquele instante.
struct EstadoCPU {
    int idCPU = 0;
    int idTarefaAtual = -1;            // -1 se o processador estiver ocioso/desligado
    int numeroOcorrenciaAtual = -1;
    int tempoExecutadoNoQuantum = 0;
};

// Registra o que aconteceu com uma ocorrência em um instante.
// Esses registros alimentam o gráfico de Gantt, incluindo execução,
// espera e eventos como chegada, conclusão, perda de prazo e sorteio.
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

// Cópia do estado completo do sistema em um tick.
// O simulador guarda esses snapshots em histórico para permitir avançar,
// voltar e consultar a execução passo a passo.
struct SnapshotSimulacao {
    long long relogioGlobal = 0;
    std::vector<EstadoCPU> cpus;

    // Para cada CPU, indica se ela ficou desligada durante o tick que terminou.
    std::vector<bool> cpusDesligadasNoTick;

    std::vector<Tarefa> tarefas;
    std::vector<RegistoGantt> registosTick;
    bool concluida = false;
};

// Representa um período contínuo em que uma CPU ficou sem executar tarefas.
// É usado para agrupar e exibir esses períodos no console e no gráfico.
struct IntervaloCPUDesligada {
    int idCPU;
    long long inicio;
    long long fim;
};

// Coordena a execução das tarefas, mantém o histórico de estados,
// permite controlar a simulação e exportar o gráfico de Gantt.
class Simulador {
public:
    // Inicializa o simulador com a configuração recebida, cria o estado do tick 0
    // e prepara as ocorrências periódicas que serão acompanhadas.
    explicit Simulador(const ConfiguracaoSistema& config);

    // Executa um tick e registra o novo estado no histórico da simulação.
    void avancarTick();

    // Retorna ao snapshot anterior, quando ele existe.
    // Retorna false se a simulação já estiver no primeiro snapshot.
    bool retrocederTick();

    // Altera manualmente o estado da ocorrência identificada pela tarefa e pelo
    // número. Essa operação é usada para testar cenários durante a simulação.
    void alterarEstadoOcorrencia(
        int idTarefa,
        int numeroOcorrencia,
        EstadoOcorrencia novoEstado);

    // Altera uma característica da tarefa identificada, como ingresso, duração,
    // período ou prazo, conforme os campos aceitos pela implementação.
    void alterarCaracteristicaTarefa(
        int idTarefa,
        const std::string& campo,
        const std::string& valor);

    // Continua avançando os ticks automaticamente até que a simulação termine.
    void executarSimulacaoCompleta();

    // Retorna uma referência somente para leitura ao snapshot do passo atual,
    // evitando copiar o estado inteiro da simulação.
    const SnapshotSimulacao& getEstadoAtual() const;

    // Informa o índice do snapshot que está sendo exibido no modo passo a passo.
    size_t getPassoAtual() const;

    // Reúne os ticks consecutivos em que cada CPU ficou desligada
    // e retorna esses períodos agrupados.
    std::vector<IntervaloCPUDesligada> getPeriodosCPUsDesligadas() const;

    // Informa se todas as tarefas da simulação já foram concluídas.
    bool estaConcluida() const;

    // Gera um arquivo SVG com a representação visual da simulação.
    // O caminho recebido define onde o arquivo será salvo.
    void exportarGanttSVG(const std::string& caminhoFicheiro) const;

private:
    // Mantém a configuração original, usada para consultar parâmetros globais
    // da simulação, como algoritmo, quantidade de CPUs e quantum.
    ConfiguracaoSistema configOriginal_;

    // Armazena uma cópia do estado do sistema em cada passo.
    // Esse histórico permite voltar no tempo e consultar ticks anteriores.
    std::vector<SnapshotSimulacao> historicoSnapshots_;

    // Indica qual snapshot do histórico está sendo exibido ou alterado atualmente.
    // Começa em zero, que representa o estado inicial, antes da execução dos ticks.
    size_t indicePassoAtual_ = 0;

    // Verifica o estado completo recebido e retorna true somente quando
    // todas as ocorrências de todas as tarefas estão concluídas.
    bool verificarConclusaoGeral(const SnapshotSimulacao& estado) const;

    std::mt19937 geradorAleatorio_;
};