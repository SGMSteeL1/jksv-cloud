# Como publicar o JKSV Cloud no GitHub

Este guia é destinado ao mantenedor do fork. Ele cobre desde a preparação do
repositório até a publicação e validação da release.

Repositório de destino:
<https://github.com/SGMSteeL1/jksv-cloud>

## 1. Entender o fluxo

O projeto usa GitHub Actions:

- push ou pull request para `main`: valida e compila, sem criar release;
- tag `vMAJOR.MINOR.PATCH`: valida, compila, empacota e publica a release;
- execução manual: compila e disponibiliza um artefato na aba Actions.

A release `1.0.1` só deve ser criada a partir do código testado. A versão deve
ser igual em todos estes lugares:

```text
Makefile                                      APP_VERSION := 1.0.1
sysmodule/include/Config.hpp                  VERSION = "1.0.1"
sysmodule/toolbox.json                        "version": "1.0.1"
sysmodule/source/MbedWebDav.cpp               JKSV-Cloud-Sync/1.0.1
tag do Git                                    v1.0.1
```

O workflow verifica essas correspondências antes de publicar.

## 2. O que nunca deve ir para o GitHub

Não faça commit de:

- `nextcloud.vault`;
- senhas, tokens, cookies ou URLs privadas;
- arquivos de save ou ZIPs da fila;
- logs pessoais do console;
- dumps, relatórios fatais com dados que você não revisou;
- diretórios `build/`;
- binários locais intermediários;
- pacotes de teste antigos.

O `.gitignore` cobre os artefatos comuns, mas sempre revise o comando
`git status` antes do commit.

## 3. Arquivos que devem fazer parte do repositório

Mantenha no GitHub:

- `.github/workflows/build-release.yml`;
- `.gitmodules` e os submódulos FsLib/SDLLib;
- `Assets`, `include`, `source`, `romfs`, `sysmodule` e arquivos de build;
- `README.md`, `HOW_TO_USE.md`, `PUBLISHING.md`, `TESTING.md`;
- `CHANGELOG.md`, `CREDITS.md`, `NOTICE.md` e `LICENSE`;
- `RELEASE_NOTES.md` atualizado para a próxima tag.

## 4. Primeira configuração do repositório

Se o diretório ainda não for um repositório Git, abra um terminal nele:

```bash
git init
git branch -M main
git remote add origin https://github.com/SGMSteeL1/jksv-cloud.git
```

Se o remoto já existe, confira:

```bash
git remote -v
```

O endereço deve apontar para `SGMSteeL1/jksv-cloud`, nunca para o repositório
original de J-D-K. O projeto original permanece referenciado nos créditos e não
deve receber os commits deste fork.

Configure sua identidade Git, se necessário:

```bash
git config user.name "Steel"
git config user.email "SEU_EMAIL_DO_GITHUB"
```

## 5. Revisar a versão 1.0.1

Antes do commit:

1. leia `RELEASE_NOTES.md` e confirme que descreve apenas mudanças reais;
2. confira se `HOW_TO_USE.md` corresponde à interface atual;
3. confirme que `LICENSE`, `NOTICE.md` e `CREDITS.md` estão presentes;
4. execute os testes de `TESTING.md`;
5. confira os arquivos alterados:

```bash
git status
git diff --check
git diff
```

`git diff --check` não deve imprimir erros de espaços ou conflitos.

## 6. Enviar a branch main

Adicione e confirme o conteúdo:

```bash
git add .
git status
git commit -m "Release JKSV Cloud 1.0.1"
git push -u origin main
```

No GitHub:

1. abra a aba **Actions**;
2. clique em **Build and release JKSV Cloud**;
3. abra a execução referente ao push da `main`;
4. aguarde o job **build** ficar verde;
5. baixe o artefato da execução, se quiser comparar os binários antes da tag.

Não crie a tag enquanto esse build estiver vermelho.

## 7. Criar e enviar a tag v1.0.1

Depois que a `main` estiver verde e apontando para o commit correto:

```bash
git switch main
git pull --ff-only origin main
git tag -a v1.0.1 -m "JKSV Cloud 1.0.1"
git push origin v1.0.1
```

Não reutilize nem mova uma tag pública. Se uma release publicada precisar de
uma correção, prepare, teste e publique uma nova versão.

## 8. O que o workflow publicará

A tag `v1.0.1` cria estes assets:

| Asset | Uso |
|---|---|
| `JKSV-Cloud.nro` | Nome fixo exigido pelo atualizador interno. |
| `JKSV-Cloud-Sync.nsp` | Sysmodule avulso para instalação/diagnóstico manual. |
| `JKSV-Cloud-1.0.1.zip` | Pacote completo com ativação automática. |
| `JKSV-Cloud-1.0.1-safe.zip` | Pacote sem `boot2.flag`. |
| `JKSV-Cloud-1.0.1-source.zip` | Código-fonte correspondente, incluindo arquivos dos submódulos. |
| `SHA256SUMS-1.0.1.txt` | Hashes SHA-256 dos cinco arquivos anteriores. |

O GitHub também disponibiliza seus arquivos automáticos **Source code (zip)** e
**Source code (tar.gz)**. O ZIP nomeado do projeto é mantido porque inclui o
conteúdo rastreado dos submódulos na mesma árvore.

O texto da página vem de `RELEASE_NOTES.md`.

## 9. Publicar pela interface do GitHub, se a automação falhar

Prefira corrigir o workflow. Se for necessário publicar manualmente:

1. abra **Releases > Draft a new release**;
2. em **Choose a tag**, selecione a tag existente `v1.0.1`;
3. use o título `JKSV Cloud 1.0.1 — nomes dos jogos e documentação completa`;
4. copie o conteúdo de `GITHUB_RELEASE_v1.0.1.md` para a descrição;
5. anexe os seis assets da pasta final de release;
6. deixe **Set as the latest release** habilitado;
7. não marque **Set as a pre-release**;
8. publique e aguarde o upload concluir.

Não crie duas releases para a mesma tag.

## 10. Validar a release pública

Depois da publicação:

1. abra <https://github.com/SGMSteeL1/jksv-cloud/releases/latest>;
2. confirme o título, a tag e as notas;
3. expanda **Assets** e confira os seis arquivos;
4. baixe o ZIP completo em um diretório vazio;
5. abra-o e confira:

```text
switch/JKSV-Cloud/boot.nro
atmosphere/contents/420000000000C10D/exefs.nsp
atmosphere/contents/420000000000C10D/cacert.pem
atmosphere/contents/420000000000C10D/toolbox.json
atmosphere/contents/420000000000C10D/flags/boot2.flag
```

6. confira que o ZIP safe contém os quatro primeiros arquivos, mas não contém
   `flags/boot2.flag`;
7. compare os hashes com `SHA256SUMS-1.0.1.txt`;
8. teste uma instalação limpa seguindo `HOW_TO_USE.md`;
9. confirme `v1.0.1` no aplicativo e no status do sysmodule;
10. feche um jogo e confirme a pasta com o nome do jogo no Nextcloud.

## 11. Testar o atualizador

Uma instalação `1.0.0` deverá reconhecer `v1.0.1` como superior e oferecer a
atualização se a release estiver pública, não for pre-release e contiver o asset
exato `JKSV-Cloud.nro`.

Como esta versão também altera o sysmodule, as notas devem continuar informando
que é obrigatório instalar o ZIP completo e reiniciar. Atualizar somente pelo
prompt não substitui `exefs.nsp`.

## 12. Regras para versões futuras

- só altere a versão quando uma melhoria estiver concluída e confirmada;
- use `vMAJOR.MINOR.PATCH` com três números;
- mantenha `RELEASE_NOTES.md` atualizado antes de criar a tag;
- mantenha `JKSV-Cloud.nro` com esse nome exato;
- não publique draft ou pre-release como atualização estável;
- não incorpore token do GitHub no aplicativo;
- não remova créditos, licença ou indicação de fork;
- toda alteração no sysmodule exige ZIP completo e reinicialização;
- preserve compatibilidade ou documente claramente qualquer migração.

## 13. Checklist final da v1.0.1

- [ ] Melhoria confirmada em console real.
- [ ] Homebrew mostra `v1.0.1`.
- [ ] Sysmodule registra `JKSV Cloud Sync v1.0.1 started`.
- [ ] Nomes de jogos aparecem em novos backups.
- [ ] Backup manual funciona.
- [ ] Backup automático funciona.
- [ ] Fila offline preserva e reenvia o ZIP.
- [ ] Restauração manual foi testada com save não crítico.
- [ ] Login e desconexão do Nextcloud funcionam.
- [ ] README aponta para `HOW_TO_USE.md`.
- [ ] J-D-K e contribuidores estão creditados visivelmente.
- [ ] Fork está identificado como independente.
- [ ] Nenhuma credencial, URL privada, log ou save está no commit.
- [ ] Build da `main` está verde.
- [ ] Tag `v1.0.1` aponta para o commit aprovado.
- [ ] Seis assets estão presentes na release.
- [ ] ZIP completo e ZIP safe têm estruturas corretas.
- [ ] Checksums conferem.
- [ ] Release está pública, estável e marcada como latest.
