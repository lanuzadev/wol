# Funcionamiento técnico

[Volver al inicio](../README.md) · [Instalación y pruebas](CONFIGURACION.md)

## Arquitectura

```mermaid
flowchart LR
    T[Telegram en el teléfono] -->|Comando por internet| A[API de Telegram]
    A -->|getUpdates por HTTPS| E[ESP32 en Wi-Fi]
    E -->|UDP broadcast: puerto 9| N[Red local]
    N -->|Magic packet para la MAC Ethernet| P[PC en espera]
```

El ESP32 inicia conexiones salientes a Telegram mediante `UniversalTelegramBot` y `WiFiClientSecure`; no expone un servidor a internet. El teléfono puede estar en otra red porque el comando viaja por Telegram. El **paquete WoL sí queda dentro de la LAN**: el ESP32 lo envía por Wi-Fi al broadcast configurado y la tarjeta Ethernet de la PC debe recibirlo. El diagrama visual de esta ruta está en la [primera imagen del README](../README.md).

## Inicio del firmware

1. `config.local.h` aporta las credenciales y direcciones específicas de la instalación. No se versiona; `config.local.example.h` documenta los campos.
2. `setup()` abre el puerto serie a **115200 baudios**, pone el ESP32 en modo estación Wi-Fi e intenta conectar. El código espera hasta conseguir `WL_CONNECTED`.
3. Si se define `TELEGRAM_ROOT_CA`, sincroniza la hora por NTP y registra la CA con `setCACert`. Si no consigue una hora válida en 20 segundos, detiene el inicio del bot para evitar un intento de validación con un reloj incorrecto.
4. Si no se define una CA, ejecuta `setInsecure()`. Esto permite que el cliente HTTPS se conecte **sin verificar el certificado del servidor**; consulta el [apartado de seguridad](#seguridad-de-la-conexión).
5. Abre un socket UDP local en el puerto 9 y comienza a consultar Telegram.

En `loop()`, cada ~1000 ms se llama a `getUpdates(bot.last_message_received + 1)`. Si hay una tanda de mensajes, se procesan y se consultan tandas adicionales hasta no recibir más. Es **sondeo**, no webhook: no requiere dirección pública ni redirección de puertos hacia el ESP32.

## Comandos y autorización

Antes del emparejamiento, `ALLOWED_CHAT_ID` está vacío. El bot responde únicamente a `/id` con el ID del chat que lo escribió. Rechaza los comandos restantes; así no puede encender la PC mientras se configura. Después de grabar el ID autorizado y volver a cargar el firmware, cada mensaje se compara con ese valor:

| Comando | Efecto en el chat autorizado |
| --- | --- |
| `/start` | Muestra la lista de comandos |
| `/id` | Repite el identificador del chat |
| `/status` | Informa SSID, IP local y RSSI del ESP32 |
| `/wake` | Llama a `sendWakeOnLan()` y comunica si la API UDP aceptó el envío |

Un ID diferente recibe `No autorizado.`. No hay cuentas múltiples, lista de usuarios, contraseñas de comandos ni confirmación adicional. El ID del chat limita quién puede pedir el encendido **mientras el token del bot siga protegido**; si el token se filtra, debe revocarse en BotFather.

## El paquete mágico

`sendWakeOnLan()` crea un arreglo de **102 bytes** y lo llena así:

```text
Bytes 0–5:    FF FF FF FF FF FF
Bytes 6–101: MAC Ethernet de la PC repetida 16 veces
Total:       6 + (16 × 6) = 102 bytes
Destino UDP: WOL_BROADCAST_IP, puerto 9
```

La MAC se define en seis bytes `PC_MAC_BYTES`. El código exige ese tamaño con `static_assert` y recorre las 16 copias con `memcpy(&packet[i * 6], pcMac, 6)`: la primera ocupa las posiciones 6–11 y la última 96–101. Este formato coincide con el evento de paquete mágico que [Microsoft describe](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/wol-methods-in-ndis-6-20) como 16 copias contiguas de la dirección Ethernet receptora.

`udp.beginPacket()`, `udp.write()` y `udp.endPacket()` indican si el ESP32 pudo construir y entregar el datagrama a la pila UDP. Ni UDP ni WoL devuelven una confirmación de que la tarjeta lo haya recibido o de que la PC haya terminado de arrancar. Por eso la respuesta del bot dice **«Paquete Wake-on-LAN enviado»**, no «PC encendida».

La dirección de broadcast determina a qué segmento de red se entrega el datagrama. Para una subred `/24` típica, los últimos ocho bits se ponen en `1`, pero otras máscaras producen otros destinos; la [guía de configuración](CONFIGURACION.md#32-crear-el-archivo-privado) explica cómo calcularlo. Un router que separe la red Wi-Fi de la Ethernet, una VLAN o el aislamiento de invitados puede impedir que llegue el paquete aunque el bot responda correctamente.

## Seguridad de la conexión

El material original tenía la contraseña Wi-Fi y el token del bot dentro de `WoL.ino`. Esta publicación los trasladó a `WoL/config.local.h`, ignorado por Git, y excluyó las capturas originales que revelaban identificadores. La [plantilla](../WoL/config.local.example.h) no incluye credenciales. Si una copia anterior expuso el token, renuévalo mediante [BotFather](https://core.telegram.org/bots/features#botfather) antes de desplegar el dispositivo.

El comportamiento sin configuración adicional utiliza `WiFiClientSecure::setInsecure()`: cifra la conexión, pero **no autentica al servidor**. Un intermediario de red podría hacerse pasar por el servicio y observar el token enviado. Para validar la identidad de Telegram, define en el archivo **local** una constante PEM `TELEGRAM_ROOT_CA` correspondiente a la cadena de certificados de `api.telegram.org`; el firmware sincroniza la hora por NTP y llama a `setCACert()`. El certificado raíz es público, pero debes obtenerlo de una fuente confiable y mantenerlo vigente cuando la cadena de Telegram cambie. Si falla NTP o la validación, el bot no podrá recibir comandos. El token sigue siendo secreto también en este modo.

`ALLOWED_CHAT_ID` es un filtro de chat, no una sustitución de la protección del token. Durante el emparejamiento cualquier interlocutor puede consultar **su propio** ID, pero no usar `/wake`. No abras UDP/9 desde internet ni publiques los datos de `config.local.h` para usar el bot remotamente: el canal remoto ya es Telegram.

## Límites actuales

- Hay **una sola MAC de destino** y una sola dirección de broadcast configuradas.
- `/status` describe el **ESP32**, no la disponibilidad de la PC.
- El firmware espera indefinidamente la conexión Wi-Fi inicial y no implementa una estrategia de reconexión posterior.
- No hay registro histórico, interfaz web, sensor físico ni control de relé.
- El éxito del despertar depende de la placa, el adaptador de red, el estado de energía y el camino del broadcast en la LAN.
