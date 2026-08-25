# Publicação do JKSV Cloud no GitHub

Repositório de destino: <https://github.com/SGMSteeL1/jksv-cloud>

O repositório já existe, é público e está vazio. Não adicione chaves, senhas,
tokens, `nextcloud.vault`, logs do Switch ou saves pessoais ao código-fonte.

## 1. Conteúdo que deve ser enviado

Envie o conteúdo da raiz deste pacote de código-fonte, incluindo:

- `.github/workflows/build-release.yml`;
- `Assets`, `Libraries`, `include`, `source`, `romfs`, `iconAssets`;
- `Makefile`, `README.md`, `PUBLISHING.md`, `TESTING.md`;
- `LICENSE`, `NOTICE.md`, `.gitignore` e demais arquivos de configuração.

Não envie artefatos locais de compilação:

- `build/`;
- `*.elf`, `*.nacp`, `*.nro`, `*.nso`, `*.pfs0`, `*.o`, `*.d`, `*.a`;
- ZIPs de teste;
- `romfs/Text/`, porque é regenerado a partir de `Assets/Text/`.

O `.gitignore` deste projeto já cobre esses arquivos.

## 2. Primeiro envio por Git

Com Git instalado e autenticado na conta `SGMSteeL1`, abra um terminal dentro
da pasta do código-fonte e execute:

```bash
git init
git branch -M main
git remote add origin https://github.com/SGMSteeL1/jksv-cloud.git
git add .
git commit -m "Release inicial do JKSV Cloud 0.3.5"
git push -u origin main
```

Confira na aba **Actions** se o workflow **Build and release JKSV Cloud** ficou
verde. Esse primeiro push compila e disponibiliza um artefato de teste, mas
ainda não cria um release público.

## 3. Criar o release inicial 0.3.5

Depois que o commit da `main` estiver no GitHub:

```bash
git tag -a v0.3.5 -m "JKSV Cloud 0.3.5"
git push origin v0.3.5
```

A tag inicia o mesmo workflow. Ele exige que `v0.3.5` corresponda exatamente a
`APP_VERSION := 0.3.5` no `Makefile`. Em seguida, publica automaticamente:

- `JKSV-Cloud.nro` — nome obrigatório para o atualizador;
- `JKSV-Cloud-0.3.5.zip` — pacote pronto para o cartão SD;
- os arquivos automáticos de código-fonte `.zip` e `.tar.gz` do GitHub.

Não marque esse release como **pre-release** e não o deixe como **draft**, pois
o endpoint `/releases/latest` usado pelo homebrew considera apenas releases
completos publicados.

## 4. Publicar versões futuras

Exemplo para a versão 0.3.6:

1. Edite `APP_VERSION := 0.3.6` no `Makefile`.
2. Faça as alterações e testes.
3. Execute `make -j1` localmente, se tiver devkitPro.
4. Faça commit e push na `main`.
5. Aguarde o build da `main` ficar verde.
6. Crie e envie a tag:

```bash
git tag -a v0.3.6 -m "JKSV Cloud 0.3.6"
git push origin v0.3.6
```

Na próxima inicialização da 0.3.5, o Switch verá `v0.3.6`, mostrará a pergunta
de atualização e, se autorizado, instalará o asset `JKSV-Cloud.nro`.

## 5. Regras para não quebrar o atualizador

- Use tags `vMAJOR.MINOR.PATCH`, sempre com três números.
- Mantenha o release como completo, publicado e definido como o mais recente.
- Nunca renomeie o asset binário `JKSV-Cloud.nro`.
- Mantenha o repositório público; a aplicação não incorpora token do GitHub.
- Não reutilize uma tag já publicada para outro binário.
- Se um build falhar, corrija o commit e crie uma nova versão; não publique o
  asset manualmente com nome ou versão divergentes.

## 6. Recuperação local

Durante uma atualização aceita, a versão anterior fica temporariamente em
`sdmc:/switch/JKSV-Cloud/boot.nro.bak`. Se a nova versão não iniciar, remova ou
renomeie `boot.nro` e renomeie `boot.nro.bak` para `boot.nro` usando um leitor de
cartão ou um gerenciador de arquivos confiável.

## 7. Checklist antes de cada tag

- [ ] `APP_TITLE := JKSV Cloud`.
- [ ] `APP_AUTHOR := JK/Steel`.
- [ ] `APP_VERSION` igual à tag sem o prefixo `v`.
- [ ] Arte personalizada presente em `icon.jpg` e `romfs/Textures`.
- [ ] Pastas locais continuam sendo `JKSV Cloud`.
- [ ] Diretório remoto continua sendo `JKSV Cloud`.
- [ ] Login por URL e QR code testados.
- [ ] Nenhuma credencial, save ou log incluído no commit.
- [ ] Build da `main` aprovado antes de enviar a tag.
