# Créditos, origem e relação com o JKSV

## Projeto original

O JKSV Cloud é baseado no [JKSV](https://github.com/J-D-K/JKSV), o gerenciador
de saves para Nintendo Switch criado e mantido por **J-D-K**, com contribuições
da comunidade.

Todo o mérito pelo aplicativo base, pelo gerenciamento de saves e pelo rewrite
moderno do JKSV pertence a J-D-K e aos respectivos contribuidores. Consulte o
[histórico de contribuidores](https://github.com/J-D-K/JKSV/graphs/contributors)
para a lista mantida pelo GitHub.

Bibliotecas incluídas como submódulos:

- [FsLib](https://github.com/J-D-K/FsLib), por J-D-K;
- [SDLLib](https://github.com/J-D-K/SDLLib), por J-D-K.

## Este fork

**JKSV Cloud** é um fork modificado e independente mantido por **Steel**
([@SGMSteeL1](https://github.com/SGMSteeL1)). Ele não é um release oficial do
JKSV e não possui afiliação ou endosso implícito do autor original.

O fork acrescenta, entre outras mudanças:

- identidade e caminhos próprios do JKSV Cloud;
- autenticação Nextcloud Login Flow v2 por QR code;
- credencial de aplicativo selada ao console;
- sysmodule opcional para backup após o fechamento do jogo;
- fila offline e reenvio automático;
- transporte WebDAV próprio sobre mbedTLS;
- status e diagnóstico do serviço em segundo plano;
- nomes oficiais dos jogos nos destinos automáticos;
- integração opcional com notificações do Ultrahand;
- atualização do NRO por GitHub Releases.

Falhas relacionadas especificamente a essas mudanças devem ser relatadas no
[repositório do fork](https://github.com/SGMSteeL1/jksv-cloud/issues), não ao
projeto JKSV original.

## Código e dados de terceiros

### Checkpoint

A implementação de armazenamento selado em
`source/security/DeviceSeal.cpp` deriva do design `device_seal` do
[Checkpoint](https://github.com/FlagBrew/Checkpoint):

- Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew e contribuidores;
- licença GPL-3.0-or-later;
- a implementação do fork usa valores mágicos e rótulos de separação de domínio
  próprios do JKSV Cloud.

### Certificados raiz

O arquivo `romfs/certs/cacert.pem` utiliza certificados do programa de CAs da
Mozilla, distribuídos pelo [CA Extract do curl](https://curl.se/docs/caextract.html).
O aviso aplicável também está incorporado no próprio arquivo.

### Demais créditos do JKSV

Créditos de interface, fontes, traduções e outras dependências herdadas
permanecem pertencendo aos autores indicados pelo projeto JKSV e pelos arquivos
de licença das dependências.

## Licença

O código derivado é distribuído sob a [GNU General Public License v3.0](LICENSE).
Ao redistribuir binários modificados, disponibilize o código-fonte
correspondente, mantenha a licença e preserve os avisos de autoria e de origem
modificada.

Esta atribuição busca deixar explícita a cadeia de autoria: **JKSV por J-D-K e
contribuidores; JKSV Cloud como fork modificado por Steel/SGMSteeL1**.
