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

Confirme os dois arquivos:

```text
sdmc:/switch/JKSV-Cloud/boot.nro
sdmc:/atmosphere/contents/420000000000C10D/exefs.nsp
```

O arquivo `exefs.nsp` não deve ser instalado pelo DBI/Tinfoil.

## Resultado esperado

```text
JKSV Cloud/Auto Sync/Nome do Jogo/
```

Pastas antigas nomeadas com Title ID não são renomeadas automaticamente. Um
novo backup usará o nome resolvido.

## Downloads

- `JKSV-Cloud-1.0.1.zip`: pacote completo recomendado;
- `JKSV-Cloud-1.0.1-safe.zip`: pacote sem ativação automática do sysmodule;
- `JKSV-Cloud.nro`: somente o aplicativo;
- `JKSV-Cloud-Sync.nsp`: somente o componente do Atmosphère;
- `JKSV-Cloud-1.0.1-source.zip`: código-fonte correspondente;
- `SHA256SUMS-1.0.1.txt`: hashes dos assets.

## Guia para iniciantes

Consulte o [guia completo de instalação e uso](HOW_TO_USE.md), com conexão ao
Nextcloud, teste ponta a ponta, fila offline, atualização, restauração,
diagnóstico e remoção.

## Créditos e licença

O [JKSV original](https://github.com/J-D-K/JKSV) foi desenvolvido por J-D-K e
seus contribuidores. JKSV Cloud é um fork independente modificado por Steel
([@SGMSteeL1](https://github.com/SGMSteeL1)); não é um release oficial do JKSV.

Distribuído sob GNU GPL v3.0. Consulte `LICENSE`, `CREDITS.md` e `NOTICE.md`.
