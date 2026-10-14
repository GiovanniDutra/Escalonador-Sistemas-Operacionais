#pragma once

#include <string>
#include <vector>

enum class EstadoOcorrencia { //Os 5 Estados das Ocorrencias das Tarefas
    Futura,
    Pronta,
    Executando,
    Suspensa,
    Concluida
};

struct Ocorrencia {
    int numero = 0;
    long long instanteAtivacao = 0;
    long long deadlineAbsoluto = 0;
    int tempoRestante = 0;

    EstadoOcorrencia estado = EstadoOcorrencia::Futura;

    bool perdeuPrazo = false;
    int cpuAtual = -1;
};

struct Tarefa {
    int id;

    std::string cor;

    int ingresso;
    int duracao;
    int periodo;
    int prazo;

    std::string eventos;

    // Guarda as 10 execucoes da tarefa no Projeto A.
    std::vector<Ocorrencia> ocorrencias;
};

struct ConfiguracaoSistema {
    std::string algoritmo;

    int quantum;
    int quantidadeCpus;
    int tarefasAperiodicasIgnoradas;
    
    std::vector<Tarefa> tarefas;
};