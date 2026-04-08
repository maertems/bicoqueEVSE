# bicoqueEVSE

Controle local et distant d'une borne de recharge [simpleEVSE](http://evracing.cz/simple-evse-wallbox) via un ESP8266, avec ecran LCD, bouton physique et interface WiFi.

## Materiel necessaire

| Composant | Detail |
|-----------|--------|
| ESP8266 | NodeMCU recommande |
| Ecran LCD | 20x4 avec interface I2C (adresse 0x27) |
| Bouton poussoir | Controle local |
| Relais | Optionnel - reset hardware de l'EVSE |
| simpleEVSE | [evracing.cz](http://evracing.cz/simple-evse-wallbox) |

## Branchements

| Fonction | GPIO | Pin NodeMCU |
|----------|------|-------------|
| Bouton | GPIO12 | D6 |
| Relais | GPIO13 | D5 |
| I2C SDA (LCD) | GPIO4 | D2 |
| I2C SCL (LCD) | GPIO5 | D1 |
| Modbus TX | GPIO1 | TX |
| Modbus RX | GPIO3 | RX |

## Fonctionnalites

- **Controle local** : demarrer/arreter la charge, regler l'amperage, activer l'autostart via le bouton et l'ecran LCD
- **Interface web** : dashboard complet accessible sur le reseau local (port 80)
- **API REST** : piloter la borne depuis n'importe quel client HTTP (domotique, scripts, etc.)
- **Suivi conso** : consommation en temps reel, par session et cumul total, sauvegarde sur SPIFFS
- **Multi-WiFi** : jusqu'a 3 reseaux WiFi configures avec priorite et failover automatique
- **OTA** : mise a jour firmware via le serveur distant (appui long > 5s sur le bouton)
- **Modbus RTU** : communication serie avec le controleur simpleEVSE (9600 bauds)

## Statuts EVSE

| Code | Statut |
|------|--------|
| 0 | N/A |
| 1 | Waiting Car |
| 2 | Car Connected |
| 3 | Charging |
| 4 | Charging |
| 5 | Error |

---

## API Reference

L'ESP expose un serveur HTTP sur le **port 80**. Toutes les reponses JSON utilisent `Content-Type: application/json`.

### Pages web

| Endpoint | Description |
|----------|-------------|
| `GET /` | Dashboard principal |
| `GET /config` | Page de configuration |
| `GET /debug` | Page debug avec tous les registres Modbus |
| `GET /wifi` | Configuration WiFi |
| `GET /setting` | Page de reglages |

### API JSON

#### `GET /jsonInfo`

Retourne les informations completes de la borne.

```json
{
  "version": "1.5.15",
  "powerHardwareLimit": 32,
  "powerOn": 16,
  "currentLimit": 10,
  "consumptionLastCharge": 4500,
  "consumptionLastTime": 3600,
  "consumptionCounter": 12,
  "consumptions": 150000,
  "consumptionActual": 2300,
  "wifiSignal": -45,
  "wifiPower": 20,
  "uptime": 86400,
  "time": 1712534400,
  "statusName": "Charging",
  "enable": 1,
  "status": 3
}
```

#### `GET /api/status`

Retourne l'etat actuel de la borne.

```json
{
  "value": "on",
  "statusCar": "Charging"
}
```

#### `POST /api/status`

Active ou desactive la borne.

**Body :**
```json
{ "action": "on" }
```

**Reponse :**
```json
{
  "msg": "Write on/off done : on",
  "action": "on"
}
```

| Valeur | Effet |
|--------|-------|
| `on` | Active la charge |
| `off` | Desactive la charge |

#### `GET /api/power`

Retourne la limite d'amperage actuelle.

```json
{
  "status": 16
}
```

#### `POST /api/power`

Modifie la limite d'amperage (0-80A). Peut etre change **pendant la charge**.

**Body :**
```json
{ "action": 16 }
```

**Reponse :**
```json
{
  "action": 16
}
```

#### `GET /api/config`

Retourne la configuration complete en JSON (WiFi, EVSE, etc.).

### Commandes via `/write`

Endpoint multi-usage par query parameters :

| Parametre | Valeur | Description |
|-----------|--------|-------------|
| `chargeon` | `yes` | Demarrer la charge |
| `chargeoff` | `yes` | Arreter la charge |
| `amperage` | `0-80` | Regler l'amperage |
| `modbus` | `0` / `1` | Activer/desactiver le Modbus |
| `status` | `0-5` | Forcer un statut EVSE |
| `register` | `<reg>&value=<val>` | Ecriture directe dans un registre Modbus |
| `autostart` | `0` / `1` | Auto-demarrage a la mise sous tension |
| `wifienable` | `0` / `1` | Activer/desactiver le WiFi |
| `relay` | `on` / `off` / `toggle` | Controle du relais |
| `clearall` | `yes` | RAZ des statistiques |
| `setConsumption` | `<value>` | Definir le compteur de consommation |

Exemples :
```
GET /write?chargeon=yes
GET /write?amperage=16
GET /write?relay=toggle
```

### Systeme de fichiers (SPIFFS)

| Endpoint | Description |
|----------|-------------|
| `GET /fs/dir?directory=<path>` | Lister le contenu d'un repertoire |
| `GET /fs/read?file=<path>` | Lire le contenu d'un fichier |
| `GET /fs/del?file=<path>` | Supprimer un fichier |
| `GET /fs/download?file=<path>` | Telecharger un fichier depuis le serveur distant |
| `GET /fs/config` | Fichier `config.json` brut |

### Autres

| Endpoint | Description |
|----------|-------------|
| `GET /reload` | Recharger les donnees Modbus |
| `GET /reboot` | Redemarrer l'ESP8266 |

---

## Configuration

La configuration est stockee dans `/config.json` sur le SPIFFS :

- **WiFi** : jusqu'a 3 reseaux (SSID, mot de passe, actif/inactif, priorite)
- **EVSE** : autostart
- **Systeme** : nom du logiciel, version

Au premier demarrage, l'ESP cree un point d'acces WiFi **"bicoqueEVSE"** pour la configuration initiale.

## Exemples d'integration

### curl

```bash
# Statut de la borne
curl http://192.168.1.x/api/status

# Activer la charge
curl -X POST http://192.168.1.x/api/status -d '{"action":"on"}'

# Regler a 16A
curl -X POST http://192.168.1.x/api/power -d '{"action":16}'

# Infos completes
curl http://192.168.1.x/jsonInfo
```

### Home Assistant (rest_command)

```yaml
rest_command:
  evse_on:
    url: "http://192.168.1.x/api/status"
    method: POST
    content_type: "application/json"
    payload: '{"action": "on"}'

  evse_set_power:
    url: "http://192.168.1.x/api/power"
    method: POST
    content_type: "application/json"
    payload: '{"action": {{ amperage }}}'
```

## Licence

Apache License 2.0
