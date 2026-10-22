#include "algoritmo.h"
// Compara duas ocorrências prontas e informa se a primeira deve vir antes
// da segunda na fila de prioridade.
//
// Critério principal:
// - RM:  tem prioridade a tarefa de menor período;
// - EDF: tem prioridade a ocorrência com o deadline absoluto mais próximo.
//
// Se o critério principal empatar, a função prefere a ocorrência que já estava
// executando, reduzindo trocas de tarefa sem necessidade. Persistindo o empate,
// compara o prazo relativo da tarefa, o instante de ativação e a duração, nessa
// ordem. Se todos esses critérios também empatarem, retorna false para que o
// simulador reconheça o empate completo e faça o sorteio previsto.
bool prioridadeAntes(
    const std::string& algoritmo,
    const Tarefa& tarefa1,
    const Ocorrencia& ocorrencia1,
    const Tarefa& tarefa2,
    const Ocorrencia& ocorrencia2) {

    // Critério principal do algoritmo escolhido.
    if (algoritmo == "RM" &&
        tarefa1.periodo != tarefa2.periodo) {
        return tarefa1.periodo < tarefa2.periodo;
    }

    if (algoritmo == "EDF" &&
        ocorrencia1.deadlineAbsoluto != ocorrencia2.deadlineAbsoluto) {
        return ocorrencia1.deadlineAbsoluto <
            ocorrencia2.deadlineAbsoluto;
    }

    // Em empate, mantém primeiro a ocorrência que já executava.
    const bool tarefa1Executava =
        ocorrencia1.estado == EstadoOcorrencia::Executando;
    const bool tarefa2Executava =
        ocorrencia2.estado == EstadoOcorrencia::Executando;

    if (tarefa1Executava != tarefa2Executava) {
        return tarefa1Executava;
    }

    // Critérios adicionais de desempate.
    if (tarefa1.prazo != tarefa2.prazo) {
        return tarefa1.prazo < tarefa2.prazo;
    }

    if (ocorrencia1.instanteAtivacao !=
        ocorrencia2.instanteAtivacao) {
        return ocorrencia1.instanteAtivacao <
            ocorrencia2.instanteAtivacao;
    }

    if (tarefa1.duracao != tarefa2.duracao) {
        return tarefa1.duracao < tarefa2.duracao;
    }

    // Empate completo: o simulador fará o sorteio.
    return false;
}