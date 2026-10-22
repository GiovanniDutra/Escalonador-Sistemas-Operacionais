#pragma once

#include <string>
#include "Modelos.h"

// Lê e valida o arquivo de configuração indicado pelo caminho.
// Retorna os parâmetros e as tarefas aceitas; lança uma exceção se o arquivo
// não puder ser aberto ou contiver dados inválidos.
ConfiguracaoSistema carregarConfiguracao(const std::string& caminho);