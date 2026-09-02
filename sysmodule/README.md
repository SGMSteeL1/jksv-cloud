# JKSV Cloud Sync

Componente opcional em segundo plano do JKSV Cloud. O sysmodule observa qual
jogo está em execução e, depois que ele é fechado, cria backups somente dos
saves `Account` e `Device` pertencentes ao Title ID encerrado.

## Segurança da primeira versão

- o save é aberto exclusivamente com `fsOpenReadOnlySaveDataFileSystem`;
- nenhuma restauração é feita em segundo plano;
- a cópia começa apenas quando não há jogo em execução;
- o `commit_id` do save evita reenviar conteúdo que não mudou;
- falhas de rede mantêm o ZIP em `sdmc:/JKSV Cloud/Sync Queue`;
- as credenciais usam o mesmo `nextcloud.vault` vinculado ao console;
- TLS permanece obrigatório e usa o pacote de CAs instalado com o sysmodule.

O destino remoto é `JKSV Cloud/Auto Sync/<Nome do Jogo>`. O Title ID é usado
como fallback quando os metadados oficiais não estão disponíveis. Os ZIPs incluem
`.nx_save_meta.bin`, portanto continuam reconhecíveis pelo fluxo de restauração
manual do JKSV Cloud.

## Status e inicialização segura

A versão 1.0.1 publica um heartbeat atômico em
`sdmc:/config/JKSV Cloud/sync-status.json`. O JKSV Cloud usa esse arquivo para
mostrar se o processo está realmente ativo, a atividade atual, o último
resultado e quantos arquivos aguardam envio.

Ela também grava notificações compatíveis com a API do Ultrahand em
`sdmc:/config/ultrahand/notifications`. Com **API Notifications** habilitado no
Ultrahand, o resultado aparece imediatamente após o fechamento do jogo. Falhas
de credencial, rede, TLS e respostas HTTP/WebDAV são registradas no status sem
expor usuário, senha ou URL.

Para evitar uma tela fatal durante o boot, o módulo espera o sistema estabilizar,
repete a montagem do SD e a abertura dos serviços sem chamar `diagAbort`, reduz
a heap estática e inicializa rede/curl somente quando a sincronização está
ativada. Uma indisponibilidade temporária deixa o módulo aguardando em vez de
derrubar o Atmosphère. No HOS 22.0.0, o módulo não abre `time:s`: utiliza ticks
monotônicos para evitar a falha de memória compartilhada durante o boot.
O NPDM autoriza explicitamente as SVCs de heap, mapeamento e consulta de memória
usadas pelo libnx, e a thread principal possui stack de 128 KiB.
As tentativas de rede são refeitas a cada dez segundos. Cada etapa WebDAV
(`MKCOL` e envio do ZIP) tem timeout e diagnóstico próprios, evitando que uma
conexão travada faça o heartbeat desaparecer. Durante a comunicação HTTP, o
callback de progresso também mantém o heartbeat atualizado.

O transporte WebDAV do worker utiliza mbedTLS diretamente, com IPv4 salvo na
configuração, TLS 1.2, SNI, certificado validado e timeouts separados. Ele não
depende do backend TLS nativo do libcurl. Após um upload, o estado da fila é
persistido antes de apagar ou mover o ZIP; a gravação possui arquivo de
recuperação `.bak` e, se falhar, o ZIP permanece na fila.

O nome do jogo é resolvido primeiro pelo `title-map.json` publicado pelo NRO e,
depois, pelos metadados Nintendo usando APIs novas, legadas e uma varredura das
entradas de idioma do NACP. O Title ID é fallback para preservar o backup.

## Instalação

O pacote completo coloca os arquivos em:

```text
sdmc:/atmosphere/contents/420000000000C10D/exefs.nsp
sdmc:/atmosphere/contents/420000000000C10D/flags/boot2.flag
sdmc:/atmosphere/contents/420000000000C10D/cacert.pem
sdmc:/atmosphere/contents/420000000000C10D/toolbox.json
```

Reinicie o console, conecte o Nextcloud no JKSV Cloud e ative **Sincronização em
segundo plano** no menu Extras. O log de diagnóstico fica em
`sdmc:/config/JKSV Cloud/JKSV-Cloud-Sync.log` e nunca registra senha ou token.

## Compilação

Com devkitA64/libnx e os portlibs `curl`, `json-c`, `minizip` e `mbedtls`:

```sh
export DEVKITPRO=/opt/devkitpro
make
```

O resultado `JKSV-Cloud-Sync.nsp` deve ser copiado como `exefs.nsp` no
diretório do Program ID acima.
