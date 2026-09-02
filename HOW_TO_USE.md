# Guia completo do JKSV Cloud

Este guia foi escrito para quem nunca instalou ou usou o JKSV Cloud. Leia uma
seção por vez e não pule os avisos sobre saves e credenciais.

## 1. Antes de começar

O JKSV Cloud é um homebrew para Nintendo Switch com Atmosphère. Ele permite
criar backups locais dos saves e enviar cópias para um servidor Nextcloud. O
componente opcional **JKSV Cloud Sync** fica em segundo plano e cria um backup
depois que um jogo é fechado.

Ele **não** é um serviço oficial da Nintendo, do Nextcloud ou do JKSV original.
O projeto é um fork independente do JKSV criado por J-D-K e seus contribuidores.

Você precisará de:

- Nintendo Switch com Atmosphère funcionando;
- acesso ao Homebrew Menu;
- cartão SD e uma forma segura de copiar arquivos para ele;
- conexão Wi-Fi configurada no Switch;
- endereço HTTPS de um servidor Nextcloud;
- usuário e senha desse Nextcloud;
- celular ou computador para abrir o link de autorização;
- pelo menos um jogo com save para realizar o teste.

> **Faça uma cópia independente antes do primeiro teste.** Use um jogo não
> crítico. Restaurar um save substitui o progresso atual.

## 2. Escolher o download correto

Abra a página [Releases](https://github.com/SGMSteeL1/jksv-cloud/releases/latest)
e expanda **Assets** se a lista estiver recolhida.

Para uma instalação comum, baixe:

```text
JKSV-Cloud-1.0.1.zip
```

Os outros arquivos têm finalidades diferentes:

| Arquivo | Finalidade |
|---|---|
| `JKSV-Cloud-1.0.1.zip` | Pacote completo recomendado. Instala o aplicativo e ativa o sysmodule no próximo boot. |
| `JKSV-Cloud-1.0.1-safe.zip` | Mesmo aplicativo, mas sem ativar o sysmodule automaticamente. |
| `JKSV-Cloud.nro` | Somente o aplicativo. Não atualiza o sysmodule. |
| `JKSV-Cloud-Sync.nsp` | Somente o executável do sysmodule. Não deve ser instalado como jogo. |
| `JKSV-Cloud-1.0.1-source.zip` | Código-fonte correspondente à release. Não é necessário para usar o aplicativo. |
| `SHA256SUMS-1.0.1.txt` | Assinaturas SHA-256 para conferir a integridade dos downloads. |

Use o pacote `safe` se já teve problemas com sysmodules, se quer testar o
console antes de ativá-lo ou se pretende gerenciá-lo pelo Ultrahand/
ovl-sysmodules. Depois de ativar o módulo, reinicie o console.

## 3. Instalar no cartão SD

### No Windows

1. Desligue corretamente o console antes de remover o cartão SD.
2. Coloque o cartão no computador.
3. Clique com o botão direito no ZIP baixado e escolha **Extrair tudo**.
4. Abra a pasta extraída. Nela devem existir as pastas `switch` e `atmosphere`.
5. Copie essas duas pastas para a raiz do cartão SD, onde já ficam as pastas do
   Atmosphère.
6. Quando o Windows perguntar, escolha **mesclar pastas** e **substituir os
   arquivos de destino**.
7. Ejete o cartão com segurança.

Não copie a pasta externa com o nome do ZIP. A estrutura correta é:

```text
SD:/switch/JKSV-Cloud/boot.nro
SD:/atmosphere/contents/420000000000C10D/exefs.nsp
SD:/atmosphere/contents/420000000000C10D/cacert.pem
SD:/atmosphere/contents/420000000000C10D/toolbox.json
SD:/atmosphere/contents/420000000000C10D/flags/boot2.flag
```

No pacote `safe`, a última linha não existirá até você habilitar o módulo.

> **Não instale `exefs.nsp` no DBI ou Tinfoil.** Apesar da extensão, esse arquivo
> é um executável de sysmodule do Atmosphère e deve ficar na pasta indicada.

## 4. Reiniciar e abrir com memória completa

1. Recoloque o cartão SD.
2. Inicie o console pelo método que você já usa para carregar o Atmosphère.
3. Faça um reinício completo; apenas fechar e abrir o Homebrew Menu não carrega
   um sysmodule novo.
4. Abra o Homebrew Menu em modo de memória completa. No método mais comum,
   mantenha **R** pressionado enquanto abre um jogo e continue segurando até o
   Homebrew Menu aparecer.
5. Inicie **JKSV Cloud**.
6. Confirme `v1.0.1` no aplicativo e aguarde a lista de usuários e jogos
   terminar de carregar.

Abrir o aplicativo uma vez é importante: ele grava
`SD:/config/JKSV Cloud/title-map.json`, usado para transformar o Title ID no
nome oficial do jogo nas pastas e notificações.

## 5. Conectar ao Nextcloud

Antes de começar, teste o endereço do Nextcloud no navegador do celular. Use a
URL base que normalmente abre a tela de login, por exemplo:

```text
https://nuvem.exemplo.com
```

No Switch:

1. Abra **JKSV Cloud**.
2. Entre em **Extras**.
3. Selecione **Conectar ao Nextcloud**.
4. Digite a URL HTTPS completa. Não digite usuário, senha ou caminho WebDAV.
5. Confirme e aguarde o QR code.
6. No celular, abra a câmera ou um leitor de QR code.
7. Acesse o link, entre na conta do Nextcloud e toque em **Conceder acesso** ou
   **Autorizar**.
8. Mantenha o JKSV Cloud aberto enquanto ele conclui a autorização.
9. Aguarde a mensagem de conexão concluída.

O Nextcloud cria uma senha exclusiva para o aplicativo. Sua senha principal
não é armazenada pelo JKSV Cloud. A credencial recebida é selada ao console em:

```text
SD:/config/JKSV Cloud/nextcloud.vault
```

Nunca publique, envie ou anexe esse arquivo. Para revogar o acesso, use
**Desconectar do Nextcloud** e, se necessário, remova a sessão/senha de
aplicativo em **Configurações pessoais > Segurança** no Nextcloud.

## 6. Criar um backup manual

Os nomes de botões podem variar conforme o idioma configurado no JKSV, mas o
fluxo geral é:

1. Abra o JKSV Cloud com o jogo fechado.
2. Selecione o usuário que possui o save.
3. Selecione o jogo.
4. Escolha a opção de criar backup.
5. Dê um nome identificável ao backup ou aceite o nome sugerido.
6. Aguarde a conclusão antes de fechar o aplicativo ou remover o cartão SD.

O backup local fica abaixo de:

```text
SD:/JKSV Cloud/
```

Quando uma entrada remota estiver disponível na interface, ela será indicada
pelo prefixo `[NC]`. O envio e a restauração manual continuam seguindo os
controles mostrados na própria tela.

## 7. Ativar o backup automático

1. Confirme que o Nextcloud está conectado.
2. Abra **Extras**.
3. Selecione **Sincronização em segundo plano: Desativada** para mudar o estado
   para **Ativada**.
4. Abra **Status da sincronização**.
5. Confirme que o módulo aparece como ativo/em execução.
6. Feche o JKSV Cloud.

Se estiver usando o pacote `safe`, habilite **JKSV Cloud Sync** pelo gerenciador
de sysmodules, reinicie e depois repita essa verificação.

## 8. Fazer o primeiro teste ponta a ponta

1. Escolha um jogo não crítico.
2. Abra o jogo e crie uma mudança fácil de identificar no progresso.
3. Salve pelo próprio jogo.
4. Volte ao menu HOME.
5. Selecione o jogo, pressione **X** e confirme **Fechar**.
6. Não abra outro jogo imediatamente. Aguarde o backup e o envio.
7. Abra o Nextcloud no celular ou computador.
8. Entre em **Arquivos > JKSV Cloud > Auto Sync**.
9. Procure a pasta com o nome oficial do jogo:

```text
JKSV Cloud/Auto Sync/Pokémon: Let's Go, Pikachu!/
```

Caracteres incompatíveis com caminhos podem ser substituídos de forma segura.
Se nenhum metadado puder ser obtido, o Title ID será usado como fallback para
que o backup não seja perdido.

10. Confirme que existe um ZIP novo dentro da pasta.
11. Reabra o JKSV Cloud e consulte **Extras > Status da sincronização**.

O módulo monitora o jogo em execução, espera cinco segundos após seu fechamento
e lê apenas os saves compatíveis `Account` e `Device`. Saves auxiliares vazios
são ignorados. Nenhuma restauração é feita em segundo plano.

## 9. O que acontece sem internet

Se o Switch não conseguir chegar ao Nextcloud, o backup não é apagado. Ele fica
em:

```text
SD:/JKSV Cloud/Sync Queue/
```

O worker tenta novamente quando o console está livre. Para testar:

1. desative o Wi-Fi;
2. altere e feche um jogo;
3. confira no status que existe item pendente;
4. reative o Wi-Fi;
5. deixe o console no HOME sem jogo em execução;
6. aguarde o reenvio e confirme o arquivo no Nextcloud.

Não apague manualmente a fila enquanto houver um upload pendente.

## 10. Notificações do Ultrahand

As notificações imediatas são opcionais. Com Ultrahand instalado, habilite
**API Notifications**. O JKSV Cloud Sync grava eventos na pasta esperada pelo
Ultrahand.

Se a notificação não aparecer, confira primeiro o painel **Status da
sincronização**. A ausência do pop-up não significa necessariamente que o
backup falhou.

## 11. Restaurar um save com segurança

Restauração é uma operação manual e substitui o save atual.

1. Feche completamente o jogo.
2. Crie antes um backup do save atual.
3. Abra o JKSV Cloud em modo de memória completa.
4. Selecione o usuário e o jogo corretos.
5. Selecione o backup local ou remoto correto.
6. Use a opção de restauração indicada na interface.
7. Leia a confirmação e confira novamente jogo, usuário e data.
8. Aguarde o fim da operação antes de sair.
9. Só então abra o jogo e valide o progresso.

O sysmodule nunca restaura saves automaticamente.

## 12. Atualizar o JKSV Cloud

Quando existe uma release pública mais nova, o aplicativo pode oferecer a
atualização do NRO. Escolher **Sim** baixa o asset `JKSV-Cloud.nro`, valida o
arquivo, preserva temporariamente `boot.nro.bak` e troca o executável.

Esse mecanismo **não atualiza um sysmodule já instalado**. Quando as notas da
release disserem que o JKSV Cloud Sync mudou:

1. baixe o ZIP completo da nova versão;
2. extraia-o novamente na raiz do SD;
3. substitua os arquivos;
4. reinicie completamente o console.

Não misture um NRO novo com um `exefs.nsp` antigo quando a release alterar os
dois componentes.

## 13. Diagnóstico e solução de problemas

### JKSV Cloud não aparece no Homebrew Menu

- confira `SD:/switch/JKSV-Cloud/boot.nro`;
- verifique se não ficou uma pasta duplicada, como
  `SD:/JKSV-Cloud-1.0.1/switch/...`;
- extraia o ZIP novamente na raiz do cartão.

### O status diz que o módulo não está em execução

- confira `SD:/atmosphere/contents/420000000000C10D/exefs.nsp`;
- no pacote completo, confira a existência de `flags/boot2.flag`;
- reinicie o Switch completamente;
- se usa o pacote safe, habilite o módulo pelo gerenciador e reinicie.

### A pasta ainda aparece como Title ID

- confirme que NRO e sysmodule são ambos `1.0.1`;
- abra o JKSV Cloud uma vez e aguarde a lista de jogos carregar;
- confira se `SD:/config/JKSV Cloud/title-map.json` existe;
- feche um jogo novamente para criar um backup novo;
- pastas antigas com Title ID não são renomeadas automaticamente;
- procure no log uma linha `Resolved title ...` para saber a origem do nome.

### Aparece “Endereço IPv4 ausente”

- abra o JKSV Cloud;
- desconecte o Nextcloud;
- conecte novamente e conclua a autorização;
- deixe o app salvar a configuração antes de fechá-lo.

### Erro de certificado TLS

- confira data, hora e fuso horário do Switch;
- teste a URL no navegador de outro aparelho;
- confirme que o servidor apresenta a cadeia completa do certificado;
- certificados autoassinados e CAs privadas não são aceitos por padrão;
- não desative a validação de certificado para contornar o erro.

### O backup fica pendente

- confira se o Wi-Fi está conectado;
- deixe o console no HOME sem jogo aberto;
- abra **Status da sincronização** para ver a etapa e o erro;
- não apague o ZIP da fila;
- se a credencial foi revogada, reconecte o Nextcloud.

### A notificação não aparece

- ative **API Notifications** no Ultrahand;
- verifique o status dentro do JKSV Cloud;
- consulte o log para confirmar sucesso ou erro.

### O console apresenta tela fatal após instalar

1. desligue o console;
2. acesse o cartão SD pelo computador;
3. mova temporariamente a pasta
   `atmosphere/contents/420000000000C10D` para fora do cartão;
4. inicie novamente o Atmosphère;
5. registre o Program ID, Result code, versão do firmware e do Atmosphère;
6. abra uma issue no fork, sem anexar saves ou `nextcloud.vault`.

## 14. Arquivos úteis para suporte

O log principal é:

```text
SD:/config/JKSV Cloud/JKSV-Cloud-Sync.log
```

O estado resumido é:

```text
SD:/config/JKSV Cloud/sync-status.json
```

Antes de abrir uma issue:

- informe a versão do JKSV Cloud;
- informe firmware e versão do Atmosphère;
- descreva a ação realizada;
- copie somente as linhas relevantes do log;
- confira se o texto não expõe dados pessoais;
- nunca envie `nextcloud.vault` ou arquivos de save.

## 15. Desconectar, desativar ou remover

### Desconectar a conta

Use **Extras > Desconectar do Nextcloud**. O aplicativo tenta revogar a senha de
aplicativo e remove o cofre local. Se o servidor estiver offline, revogue-a
também nas configurações de segurança do Nextcloud.

### Desativar apenas o backup automático

Altere **Sincronização em segundo plano** para **Desativada**. Também é possível
desabilitar o sysmodule pelo Ultrahand/ovl-sysmodules e reiniciar.

### Remover o aplicativo

Apague somente estes componentes de programa:

```text
SD:/switch/JKSV-Cloud/
SD:/atmosphere/contents/420000000000C10D/
```

As pastas abaixo contêm backups, fila e configuração. Só as remova depois de
guardar o que desejar; a exclusão não poderá ser desfeita:

```text
SD:/JKSV Cloud/
SD:/config/JKSV Cloud/
```

Depois de remover o sysmodule, reinicie o console.

## 16. Créditos e suporte correto

- Projeto base: [JKSV](https://github.com/J-D-K/JKSV), por J-D-K e
  contribuidores.
- Fork e recursos de nuvem: Steel
  ([@SGMSteeL1](https://github.com/SGMSteeL1)).
- Problemas específicos do backup automático, Nextcloud, login, atualização ou
  identidade JKSV Cloud devem ser abertos no
  [repositório do fork](https://github.com/SGMSteeL1/jksv-cloud/issues), não no
  projeto original.

Consulte [CREDITS.md](CREDITS.md) para todas as atribuições.
