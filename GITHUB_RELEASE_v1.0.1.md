# JKSV Cloud 1.0.1 — nomes dos jogos e documentação completa

Esta é a primeira atualização confirmada após a versão 1.0.0. Ela corrige o
uso do Title ID nas pastas do backup automático e entrega documentação ponta a
ponta para usuários iniciantes.

## O que foi corrigido

- nomes oficiais dos jogos nas pastas locais e no Nextcloud;
- nomes dos jogos nos arquivos ZIP e nas notificações;
- mapa persistente `Title ID → nome`, criado pelo próprio JKSV Cloud;
- consulta de metadados compatível com APIs novas e legadas do Horizon;
- busca em todos os idiomas disponíveis no NACP;
- codificação segura do nome nos caminhos WebDAV;
- Title ID usado somente se nenhuma fonte de metadados responder;
- log indicando se o nome veio do mapa, dos metadados Nintendo ou do fallback.

## Instalação obrigatória

Esta versão altera **o aplicativo e o sysmodule**. O atualizador interno troca
somente o NRO, portanto instale o pacote completo:

1. baixe `JKSV-Cloud-1.0.1.zip`;
2. extraia todo o conteúdo na raiz do cartão SD;
3. substitua os arquivos existentes;
4. reinicie completamente o Switch;
5. abra o JKSV Cloud uma vez e aguarde a lista de jogos carregar;
6. feche um jogo para testar um novo backup.

O arquivo `exefs.nsp` deve permanecer em
`atmosphere/contents/420000000000C10D` e não deve ser instalado pelo DBI ou
Tinfoil.

## Resultado esperado

```text
JKSV Cloud/Auto Sync/Nome do Jogo/
```

Pastas antigas nomeadas com Title ID não são renomeadas automaticamente. Um
novo backup usará o nome resolvido.

## Qual arquivo baixar?

- **Maioria dos usuários:** `JKSV-Cloud-1.0.1.zip`.
- **Ativação manual do sysmodule:** `JKSV-Cloud-1.0.1-safe.zip`.
- **Somente aplicativo:** `JKSV-Cloud.nro`.

O guia detalhado está em
[HOW_TO_USE.md](https://github.com/SGMSteeL1/jksv-cloud/blob/v1.0.1/HOW_TO_USE.md).

## Créditos

O [JKSV original](https://github.com/J-D-K/JKSV) foi desenvolvido por **J-D-K
e seus contribuidores**. O JKSV Cloud é um fork independente modificado por
**Steel ([@SGMSteeL1](https://github.com/SGMSteeL1))** e não é um release
oficial do projeto original.

Distribuído sob GNU GPL v3.0. Consulte `LICENSE`, `CREDITS.md` e `NOTICE.md`.
