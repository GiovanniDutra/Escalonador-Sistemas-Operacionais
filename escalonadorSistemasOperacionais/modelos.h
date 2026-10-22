#pragma once

#include <string>
#include <vector>

// Estados possíveis de uma ocorrência durante a simulação.
// O estado muda conforme a ocorrência aguarda ativação, fica pronta,
// recebe CPU, é suspensa ou termina.
enum class EstadoOcorrencia { //Os 5 Estados das Ocorrencias das Tarefas
    Futura,
    Pronta,
    Executando,
    Suspensa,
    Concluida
};

// Representa uma execução específica de uma tarefa periódica.
// Guarda os tempos e o estado dessa ocorrência sem misturá-los com os
// dados permanentes da tarefa.
struct Ocorrencia {
    int numero = 0;                     // Posição desta ocorrência na sequência da tarefa, de 1 a 10.
    long long instanteAtivacao = 0;     // Tick a partir do qual a ocorrência pode ser executada.
    long long deadlineAbsoluto = 0;     // Tick limite, calculado a partir da ativação e do prazo.
    int tempoRestante = 0;              // Quantidade de ticks de CPU ainda necessária para concluir.

    EstadoOcorrencia estado = EstadoOcorrencia::Futura; // Estado atual desta ocorrência.

    bool perdeuPrazo = false;           // Indica se o deadline já foi ultrapassado.
    int cpuAtual = -1;                  // CPU associada; -1 indica que nenhuma CPU está executando a ocorrência.
};

// Guarda as características comuns a todas as ocorrências de uma tarefa,
// além da lista das ocorrências que serão acompanhadas pelo simulador.
struct Tarefa {
    int id;                 // Identificador único usado para localizar a tarefa.

    std::string cor;        // Cor hexadecimal usada para desenhar a tarefa no gráfico.

    int ingresso;           // Tick de ativação da primeira ocorrência.
    int duracao;            // Quantidade de ticks de CPU necessária para cada ocorrência.
    int periodo;            // Intervalo entre ativações consecutivas.
    int prazo;              // Prazo relativo contado a partir da ativação de cada ocorrência.

    std::string eventos;    // Campo opcional de eventos recebido na configuração.

    // Guarda as 10 execucoes da tarefa no Projeto A.
    std::vector<Ocorrencia> ocorrencias;
};

// Reúne os parâmetros globais lidos do arquivo de configuração e as
// tarefas periódicas aceitas para a simulação.
struct ConfiguracaoSistema {
    std::string algoritmo;              // Algoritmo de escalonamento escolhido: RM ou EDF.

    int quantum;                        // Limite de ticks consecutivos de execução antes do fim do quantum.
    int quantidadeCpus;                 // Número de CPUs que o simulador deve considerar.
    int tarefasAperiodicasIgnoradas;    // Contador de tarefas aperiódicas não simuladas.

    std::vector<Tarefa> tarefas;        // Tarefas periódicas aceitas para a simulacao.
};