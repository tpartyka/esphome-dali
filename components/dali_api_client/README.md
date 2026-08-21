# `dali_api_client` external component

Creates ESPHome `light` entities on a second ESP that control a DALI controller's
existing REST cockpit API. It requires the controller to expose `/api/lamp` and
`/api/scene` (the `dali` component's `expose_dashboard: true` endpoint).

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [dali_api_client]

http_request:
  id: dali_http
  timeout: 1s

dali_api_client:
  id: dali_remote
  http_request_id: dali_http
  base_url: http://dali-controller.local:8080

light:
  - platform: dali_api_client
    name: Salon DALI
    dali_api_client_id: dali_remote
    address: 3                 # 0..63 for target: lamp

  - platform: dali_api_client
    name: DALI Group 0
    dali_api_client_id: dali_remote
    target: group
    address: 0                 # 0..15 for a group
    supports_color_temperature: true
    cold_white_color_temperature: 6500K
    warm_white_color_temperature: 2700K

interval:
  - interval: 60s
    then:
      - dali_api_client.recall_scene:
          dali_api_client_id: dali_remote
          scene: 4             # 0..15
          target: group        # all (default), lamp, or group
          address: 0           # ignored when target: all
```

The remote controller returns `200 queued` when it accepted a command. That
confirms queueing, not that the DALI device executed it. The light state on the
client ESP is therefore optimistic; this first version does not poll
`/api/lamps` to reflect changes made by another controller.

The REST cockpit API has no authentication. Keep controller and client on a
trusted management LAN/VLAN; do not expose its port to the Internet.
