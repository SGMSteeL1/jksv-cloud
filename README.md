# JKSV Cloud

[![Build](https://github.com/SGMSteeL1/jksv-cloud/actions/workflows/build-release.yml/badge.svg)](https://github.com/SGMSteeL1/jksv-cloud/actions/workflows/build-release.yml)
[![Release](https://img.shields.io/github/v/release/SGMSteeL1/jksv-cloud)](https://github.com/SGMSteeL1/jksv-cloud/releases/latest)
[![License](https://img.shields.io/github/license/SGMSteeL1/jksv-cloud)](LICENSE)

JKSV Cloud é uma modificação do JKSV para Nintendo Switch com integração
intuitiva ao Nextcloud, identificação própria de pastas e atualização pelo
GitHub Releases.

Este projeto não é um release oficial do JKSV. O código derivado continua sob
a GNU General Public License v3.0. Consulte `LICENSE` e `NOTICE.md`.

## Recursos desta versão

- Nome do aplicativo: **JKSV Cloud**.
- Autor exibido no Homebrew Menu: **JK/Steel**.
- Login em qualquer servidor Nextcloud compatível por URL HTTPS.
- Nextcloud Login Flow v2: a senha normal da conta não é entregue ao homebrew.
- QR code exibido dentro do aplicativo, sem depender do applet de navegador do
  Nintendo Switch.
- Credencial de aplicativo selada para o console em
  `sdmc:/config/JKSV Cloud/nextcloud.vault`.
- Backup local em `sdmc:/JKSV Cloud`.
- Configuração em `sdmc:/config/JKSV Cloud`.
- Diretório remoto `JKSV Cloud` no Nextcloud.
- Verificação automática de novas versões publicadas neste repositório.
- Download seguro para arquivo temporário, validação do cabeçalho NRO e troca
  com cópia de recuperação `boot.nro.bak`.

O suporte original do JKSV para saves, ZIP, Google Drive e WebDAV permanece no
código. O fluxo recomendado desta modificação é o Nextcloud.

## Instalação

1. Baixe o ZIP da versão mais recente em
   [Releases](https://github.com/SGMSteeL1/jksv-cloud/releases/latest).
2. Extraia a pasta `switch` para a raiz do cartão SD.
3. Confirme que o executável ficou em
   `sdmc:/switch/JKSV-Cloud/boot.nro`.
4. Abra **JKSV Cloud** pelo Homebrew Menu com acesso total à memória.

Antes do primeiro teste, mantenha um backup independente de um save não
crítico.

## Conectar ao Nextcloud

1. Abra o menu **Extras**.
2. Selecione **Conectar ao Nextcloud**.
3. Digite a URL HTTPS completa do servidor desejado.
4. Escaneie com o celular o QR code mostrado no Switch.
5. Entre na conta e autorize o aplicativo.

O servidor cria uma senha de aplicativo revogável para este console. Para trocar
de servidor, desconecte a conta atual em **Extras** e execute a conexão de novo.

## Atualizações automáticas

Ao iniciar, a aplicação consulta uma única vez:

`https://api.github.com/repos/SGMSteeL1/jksv-cloud/releases/latest`

Se a `tag_name` for semanticamente superior à versão compilada, por exemplo
`v0.3.6` acima de `0.3.5`, e o release contiver o asset exato
`JKSV-Cloud.nro`, o usuário recebe uma confirmação. A atualização só acontece
depois de escolher **Sim**.

Releases em rascunho e pré-releases não são oferecidos pelo atualizador. O
repositório deve permanecer público para que o Switch consulte a API sem token.

## Compilar localmente

Requisitos:

- devkitPro com devkitA64 e libnx;
- Python 3;
- portlibs do Switch: bzip2, curl, freetype, harfbuzz, libjpeg-turbo,
  libjson-c, libpng, libwebp, SDL2, SDL2_image, tinyxml2 e zlib.

As dependências FsLib e SDLLib estão incluídas na árvore de código. Com o
ambiente devkitPro configurado:

```bash
make -j1
```

O resultado será `JKSV-Cloud.nro`. O workflow em
`.github/workflows/build-release.yml` usa a imagem
`devkitpro/devkita64:latest` para repetir essa compilação no GitHub Actions.

## Publicar uma versão

As instruções completas para o primeiro envio e para releases futuros estão em
[`PUBLISHING.md`](PUBLISHING.md). O fluxo resumido é:

1. alterar somente `APP_VERSION` no `Makefile`;
2. fazer commit e enviar para `main`;
3. criar uma tag no formato `vMAJOR.MINOR.PATCH` com o mesmo número;
4. enviar a tag.

O GitHub Actions valida a versão, compila, monta o ZIP do cartão SD e publica
automaticamente `JKSV-Cloud.nro` e `JKSV-Cloud-VERSAO.zip` no release.

## Créditos e licença

- JKSV e o rewrite original: J-D-K e contribuidores.
- Modificação JKSV Cloud: JK/Steel.
- Selagem de credenciais derivada do design `device_seal` do Checkpoint;
  detalhes e atribuições em `NOTICE.md`.
- Certificados raiz: programa de certificados da Mozilla, distribuídos pelo
  serviço CA Extract do curl.

Distribuído sob a GNU GPL v3.0. Ao redistribuir binários, disponibilize também o
código-fonte correspondente e preserve `LICENSE`, `NOTICE.md` e os créditos.
