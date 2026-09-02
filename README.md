# JKSV Cloud

[![Build](https://github.com/SGMSteeL1/jksv-cloud/actions/workflows/build-release.yml/badge.svg)](https://github.com/SGMSteeL1/jksv-cloud/actions/workflows/build-release.yml)
[![Release](https://img.shields.io/github/v/release/SGMSteeL1/jksv-cloud)](https://github.com/SGMSteeL1/jksv-cloud/releases/latest)
[![License](https://img.shields.io/github/license/SGMSteeL1/jksv-cloud)](LICENSE)

JKSV Cloud é um fork independente do [JKSV](https://github.com/J-D-K/JKSV),
criado para facilitar backups de saves do Nintendo Switch em um servidor
Nextcloud. Além dos recursos do JKSV, este fork adiciona conexão guiada por QR
code, armazenamento protegido da credencial, atualização pelo GitHub e um
sysmodule opcional que cria o backup quando o jogo é fechado.

> **Crédito principal:** o aplicativo e a base de gerenciamento de saves foram
> desenvolvidos por **J-D-K e pelos contribuidores do JKSV**. O **JKSV Cloud** é
> uma modificação mantida por **Steel (SGMSteeL1)**. Este projeto não é uma
> versão oficial, não é afiliado ao projeto original e não deve direcionar ao
> autor original pedidos de suporte específicos deste fork.

Distribuído sob a [GNU General Public License v3.0](LICENSE). Consulte também
[CREDITS.md](CREDITS.md) e [NOTICE.md](NOTICE.md).

## Comece por aqui

- **Usuário iniciante:** leia o [guia completo de instalação e uso](HOW_TO_USE.md).
- **Quer apenas instalar:** baixe `JKSV-Cloud-1.0.1.zip` em
  [Releases](https://github.com/SGMSteeL1/jksv-cloud/releases/latest), extraia-o
  na raiz do cartão SD e reinicie o console.
- **Quer publicar ou manter o fork:** siga [PUBLISHING.md](PUBLISHING.md).
- **Quer validar uma compilação:** consulte [TESTING.md](TESTING.md).

## O que o fork adiciona

- conexão com qualquer Nextcloud compatível usando URL HTTPS e Login Flow v2;
- autorização pelo celular por QR code, sem digitar a senha no Switch;
- senha de aplicativo revogável, selada ao console em
  `sdmc:/config/JKSV Cloud/nextcloud.vault`;
- backup local em `sdmc:/JKSV Cloud` e pasta remota `JKSV Cloud`;
- sysmodule **JKSV Cloud Sync** para backup automático após fechar o jogo;
- pastas, ZIPs e notificações identificados pelo nome oficial do jogo;
- Title ID somente como fallback quando nenhum metadado está disponível;
- fila offline persistente em `sdmc:/JKSV Cloud/Sync Queue`;
- WebDAV próprio sobre mbedTLS, TLS 1.2, SNI e validação de certificado;
- timeouts separados de conexão TCP, handshake TLS e operação HTTP;
- worker de upload separado do monitor de jogos;
- painel de status, log de diagnóstico e notificações opcionais via Ultrahand;
- atualização voluntária do arquivo NRO por GitHub Releases;
- restauração somente manual, com o jogo fechado.

## Instalação rápida

Requisitos mínimos:

- Nintendo Switch capaz de executar Atmosphère e Homebrew Menu;
- cartão SD com espaço livre;
- conexão à internet no Switch;
- servidor Nextcloud acessível por HTTPS, com certificado confiável;
- um celular ou computador para autorizar a conexão.

Procedimento:

1. Faça um backup de segurança de um save não crítico.
2. Baixe `JKSV-Cloud-1.0.1.zip` na página da release.
3. Extraia as pastas `switch` e `atmosphere` para a **raiz** do cartão SD.
4. Aceite a substituição de arquivos de uma versão anterior.
5. Recoloque o cartão e reinicie completamente o Switch.
6. Abra o Homebrew Menu em modo de memória completa e inicie **JKSV Cloud**.
7. Aguarde a tela principal terminar de carregar. Isso também cria o mapa que
   permite ao sysmodule usar o nome dos jogos.

Arquivos principais instalados:

```text
sdmc:/switch/JKSV-Cloud/boot.nro
sdmc:/atmosphere/contents/420000000000C10D/exefs.nsp
sdmc:/atmosphere/contents/420000000000C10D/cacert.pem
sdmc:/atmosphere/contents/420000000000C10D/toolbox.json
sdmc:/atmosphere/contents/420000000000C10D/flags/boot2.flag
```

O arquivo `exefs.nsp` **não é um jogo e não deve ser instalado pelo DBI,
Tinfoil ou outro instalador**. Ele deve permanecer exatamente na pasta
`atmosphere/contents/420000000000C10D`.

### Pacote completo ou safe?

| Pacote | Comportamento | Indicado para |
|---|---|---|
| `JKSV-Cloud-1.0.1.zip` | Inclui `boot2.flag`; o sysmodule inicia junto com o Atmosphère. | Instalação normal. |
| `JKSV-Cloud-1.0.1-safe.zip` | Não inclui `boot2.flag`; o sysmodule fica desativado até ser habilitado manualmente. | Diagnóstico, primeira inicialização cautelosa ou usuários do Ultrahand/ovl-sysmodules. |

Ao habilitar ou desabilitar o sysmodule, reinicie o console.

## Conectar ao Nextcloud

1. Abra **JKSV Cloud**.
2. Entre no menu **Extras**.
3. Selecione **Conectar ao Nextcloud**.
4. Digite a URL completa, incluindo `https://` e qualquer subpasta usada pelo
   servidor.
5. Escaneie o QR code com o celular.
6. Entre no Nextcloud no celular e autorize o aplicativo.
7. Volte ao Switch e aguarde a confirmação.

O Login Flow v2 entrega ao JKSV Cloud uma senha de aplicativo revogável. A
senha normal da conta não é fornecida ao homebrew. Para trocar de servidor ou
conta, use **Extras > Desconectar do Nextcloud** e conecte novamente.

## Ativar e testar o backup automático

1. Confirme que instalou o pacote completo e reiniciou o console.
2. Abra o JKSV Cloud uma vez e aguarde a lista de jogos aparecer.
3. Em **Extras**, altere **Sincronização em segundo plano** para **Ativada**.
4. Abra **Status da sincronização** e confirme que o módulo está em execução.
5. Abra um jogo, faça uma alteração visível no save e volte ao menu HOME.
6. Feche o jogo completamente com **X > Fechar**.
7. Aguarde alguns segundos sem abrir outro jogo.
8. No Nextcloud, confira:

```text
JKSV Cloud/Auto Sync/<Nome do Jogo>/
```

O sysmodule aguarda o encerramento do processo do jogo, abre saves `Account` e
`Device` somente para leitura e evita repetir saves cujo `commit_id` não mudou.
Se não houver rede, o ZIP permanece na fila local e será reenviado quando o
console estiver livre e a conexão voltar.

## Pastas e arquivos de diagnóstico

| Caminho | Conteúdo | Pode compartilhar? |
|---|---|---|
| `sdmc:/JKSV Cloud` | Backups locais. | Não; contém saves pessoais. |
| `sdmc:/JKSV Cloud/Sync Queue` | Backups aguardando envio. | Não; contém saves pessoais. |
| `sdmc:/config/JKSV Cloud/nextcloud.vault` | Credencial de aplicativo selada ao console. | **Nunca.** |
| `sdmc:/config/JKSV Cloud/title-map.json` | Mapa de Title ID para nome. | Sim, depois de revisar. |
| `sdmc:/config/JKSV Cloud/sync-status.json` | Estado resumido do sysmodule. | Sim, depois de revisar. |
| `sdmc:/config/JKSV Cloud/JKSV-Cloud-Sync.log` | Log técnico sem senha ou token. | Sim, depois de revisar. |

## Atualizações

O aplicativo consulta a release pública mais recente de
`SGMSteeL1/jksv-cloud` uma vez ao iniciar. Só oferece uma atualização quando a
tag publicada é semanticamente superior à versão instalada e existe um asset
chamado exatamente `JKSV-Cloud.nro`.

O atualizador interno troca **somente o NRO**. Se as notas da nova versão
informarem que o sysmodule mudou, instale novamente o ZIP completo e reinicie.
Releases em rascunho ou marcadas como pré-release não são oferecidas.

## Segurança e limitações

- use primeiro um jogo não crítico e mantenha uma cópia independente;
- a restauração substitui o save atual e nunca é executada pelo sysmodule;
- feche o jogo antes de restaurar qualquer save;
- certificados autoassinados ou emitidos por CA privada não são aceitos pela
  configuração padrão;
- a credencial selada não deve ser copiada nem publicada;
- mods de sistema e homebrews são usados por conta e risco do usuário;
- esta versão foi validada no ambiente de desenvolvimento informado nas notas
  da release; outras combinações de firmware e Atmosphère podem exigir testes.

## Compilar

Instale devkitPro com devkitA64, libnx, Python 3 e os portlibs do Switch usados
pelo JKSV: bzip2, curl, freetype, harfbuzz, libjpeg-turbo, libjson-c, libpng,
libwebp, SDL2, SDL2_image, tinyxml2, minizip, mbedTLS e zlib. Clone com os
submódulos e compile:

```bash
git clone --recurse-submodules https://github.com/SGMSteeL1/jksv-cloud.git
cd jksv-cloud
make clean
make -j1
```

Saídas esperadas:

```text
JKSV-Cloud.nro
sysmodule/JKSV-Cloud-Sync.nsp
```

O workflow em `.github/workflows/build-release.yml` repete a compilação em uma
imagem oficial do devkitPro, valida as versões e monta os ZIPs.

## Créditos

- **JKSV e seu rewrite:** [J-D-K](https://github.com/J-D-K) e
  [contribuidores](https://github.com/J-D-K/JKSV/graphs/contributors).
- **Fork JKSV Cloud:** Steel
  ([@SGMSteeL1](https://github.com/SGMSteeL1)).
- **FsLib e SDLLib:** J-D-K.
- **Selagem de credenciais:** baseada no design `device_seal` do
  [Checkpoint](https://github.com/FlagBrew/Checkpoint), de Bernardo Giordano,
  FlagBrew e contribuidores.
- **Certificados raiz:** programa da Mozilla, pacote obtido pelo
  [CA Extract do curl](https://curl.se/docs/caextract.html).

A relação completa de origem, licenças e alterações está em
[CREDITS.md](CREDITS.md) e [NOTICE.md](NOTICE.md).
