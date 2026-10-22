#pragma once

#include "modelos.h"
#include <string>

// Compara duas ocorrências segundo o algoritmo informado.
// Retorna true quando a primeira deve ser ordenada antes da segunda.
// Em empate completo, retorna false para permitir que o simulador trate o empate.
bool prioridadeAntes(
    const std::string& algoritmo,
    const Tarefa& tarefa1,
    const Ocorrencia& ocorrencia1,
    const Tarefa& tarefa2,
    const Ocorrencia& ocorrencia2);