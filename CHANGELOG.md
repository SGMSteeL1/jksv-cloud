# Changelog

Todas as alterações relevantes do JKSV Cloud são documentadas neste arquivo.
O projeto usa versões no formato `MAJOR.MINOR.PATCH`.

## [1.0.1] - 2026-09-02

### Corrigido

- resolução confiável do nome oficial do jogo no sysmodule;
- consulta aos metadados Nintendo por API nova e legada;
- varredura das entradas de idioma do NACP quando o idioma desejado não existe;
- mapa persistente `Title ID → nome`, gerado pelo próprio JKSV Cloud;
- uso do nome resolvido nas pastas locais, no Nextcloud, nos ZIPs e nas
  notificações;
- codificação URL do nome da pasta remota;
- Title ID mantido apenas como fallback seguro;
- identificação da origem do nome no log.

### Documentação

- guia de instalação e uso ponta a ponta para iniciantes;
- instruções completas de publicação no GitHub;
- créditos e relação com o JKSV original destacados;
- avisos de segurança, atualização, diagnóstico e remoção ampliados.

## [1.0.0] - 2026-09-02

Primeira versão estável do fork JKSV Cloud, com integração Nextcloud,
credencial selada, sysmodule de backup automático, fila offline, WebDAV sobre
mbedTLS, status, diagnóstico e atualização por GitHub Releases.

[1.0.1]: https://github.com/SGMSteeL1/jksv-cloud/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/SGMSteeL1/jksv-cloud/releases/tag/v1.0.0
