# Plano de testes — JKSV Cloud 1.0.1

Use um jogo não crítico e mantenha um backup independente. Registre firmware,
versão do Atmosphère, modelo do console e versão do Nextcloud.

## 1. Instalação

- [ ] O ZIP completo extrai sem erro.
- [ ] `switch/JKSV-Cloud/boot.nro` existe.
- [ ] `atmosphere/contents/420000000000C10D/exefs.nsp` existe.
- [ ] `cacert.pem`, `toolbox.json` e `flags/boot2.flag` existem.
- [ ] O ZIP safe contém os mesmos arquivos, exceto `boot2.flag`.
- [ ] O console reinicia sem tela fatal.
- [ ] O Homebrew Menu mostra JKSV Cloud `v1.0.1`.
- [ ] O status/log mostra JKSV Cloud Sync `v1.0.1`.

## 2. Mapa de títulos

1. Abra o JKSV Cloud e aguarde a lista terminar de carregar.
2. Confirme a existência de:

```text
sdmc:/config/JKSV Cloud/title-map.json
```

3. Confira se o objeto `titles` não está vazio.
4. Confirme que pelo menos um Title ID conhecido aponta para o nome exibido no
   menu HOME.

## 3. Conexão Nextcloud

- [ ] Digitar URL HTTPS abre o QR code.
- [ ] Autorizar no celular conclui o Login Flow v2.
- [ ] `nextcloud.vault` é criado.
- [ ] Nenhuma senha/token aparece no log.
- [ ] Desconectar remove o cofre local e permite conectar novamente.

## 4. Backup manual e restauração

1. Crie um backup local de um jogo não crítico.
2. Envie-o ao Nextcloud e confirme a entrada remota.
3. Faça uma segunda alteração no save e um segundo backup.
4. Com o jogo fechado, restaure o primeiro backup.
5. Abra o jogo e valide o progresso esperado.
6. Restaure o backup mais recente, se desejado.

## 5. Backup automático e nome do jogo

1. Ative a sincronização em segundo plano.
2. Confirme o módulo em **Status da sincronização**.
3. Abra um jogo, altere o save e feche-o com **X > Fechar**.
4. Aguarde o worker terminar.
5. Confirme no Nextcloud:

```text
JKSV Cloud/Auto Sync/<Nome do Jogo>/
```

6. Confirme que o ZIP e a notificação usam o nome do jogo.
7. Confirme no log uma linha semelhante a:

```text
Resolved title 0100... as 'Nome do Jogo' from the JKSV Cloud title map.
```

ou:

```text
Resolved title 0100... as 'Nome do Jogo' from Nintendo metadata.
```

8. Repita com jogos de regiões/idiomas diferentes.
9. Confirme que pastas antigas por Title ID permanecem intactas e que o novo
   backup usa a pasta legível.

## 6. Save sem alterações

1. Feche novamente o mesmo jogo sem alterar seu save.
2. Confirme que o log informa save inalterado.
3. Confirme que nenhum ZIP duplicado é enviado.

## 7. Fila offline

1. Desative o Wi-Fi.
2. Altere e feche um jogo.
3. Confirme o ZIP em `sdmc:/JKSV Cloud/Sync Queue`.
4. Confirme o item pendente no status.
5. Reative o Wi-Fi e deixe o console no HOME.
6. Confirme o envio automático e a remoção segura do item concluído.

## 8. Falhas controladas

- URL inválida retorna erro sem travar o monitor.
- Credencial revogada mantém o ZIP na fila.
- Certificado inválido é rejeitado.
- Timeout TCP/TLS/HTTP termina e atualiza o status.
- Um upload pendente não impede detectar o fechamento de outro jogo.
- Saves auxiliares vazios são ignorados sem falso sucesso.

## 9. Notificações e painel

- Com API Notifications habilitada no Ultrahand, sucesso aparece após o envio.
- Falha de rede aparece como item pendente, não como backup perdido.
- O painel mostra atividade, último resultado e quantidade pendente.
- O heartbeat não expira durante uma operação WebDAV demorada.

## 10. Atualizador

Uma instalação `1.0.0` deve reconhecer `v1.0.1` quando a release estiver
publicada e contiver `JKSV-Cloud.nro`.

- escolher **Não** não altera arquivos;
- escolher **Sim** instala o NRO e mantém `boot.nro.bak` temporariamente;
- o NRO instalado mostra `v1.0.1`;
- o teste também confirma que o sysmodule continua exigindo instalação pelo ZIP
  completo e reinicialização.

## 11. Evidências para a release

Antes de criar a tag, guarde localmente:

- hashes SHA-256 dos assets;
- saída do build limpo;
- lista interna dos dois ZIPs;
- trecho do log com versão, nome resolvido e HTTP de sucesso;
- captura da pasta remota usando o nome do jogo.

Não publique `nextcloud.vault`, saves, tokens ou URLs privadas.
