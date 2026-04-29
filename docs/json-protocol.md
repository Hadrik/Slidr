# JSON Protocol (v2)

Transport: JSON Lines over USB UART.
- One JSON object per line.
- UTF-8 only.
- Every request must include a unique message_id.

## Envelope

Request envelope:

```json
{
  "protocol_version": "1.0",
  "message_id": "abc-123",
  "kind": "config.update_field",
  "target": "runtime",
  "payload": {}
}
```

Response envelope:

```json
{
  "protocol_version": "1.0",
  "kind": "response",
  "message_id": "abc-123",
  "timestamp_ms": 123456,
  "status": "ok",
  "error_code": "ok",
  "payload": {}
}
```

Status values:
- ok
- restart_required
- error

## Commands

### ping
Request payload:

```json
{}
```

Response payload:

```json
{
  "pong": true
}
```

### heartbeat
Request payload:

```json
{}
```

Response payload:

```json
{
  "alive": true,
  "filesystem_ready": true,
  "pending_restart_components": []
}
```

### config.get
Request payload:

```json
{}
```

Response payload:

```json
{
  "document": {
    "components": []
  }
}
```

### config.apply_full
Request payload:

```json
{
  "document": {
    "components": [
      {
        "id": "slider_0",
        "type": "slider",
        "options": {
          "pin": 1,
          "deadzone": 2,
          "min_value": 0,
          "max_value": 100,
          "adc_bits": 12
        }
      }
    ]
  }
}
```

### config.update_field
Request payload:

```json
{
  "component_id": "slider_0",
  "field": "deadzone",
  "value": 3
}
```

Response payload:

```json
{
  "component_id": "slider_0",
  "field": "deadzone",
  "restart_required": false
}
```

If a field is restart-required, status is restart_required and payload includes pending_restart_components.

### file.list
Request payload:

```json
{
  "path": "/"
}
```

`path` is optional. `/` returns virtual root entries (`/config`, `/images`).

Response payload:

```json
{
  "path": "/",
  "count": 2,
  "entries": [
    {
      "path": "/config",
      "size": 0,
      "modified_ms": 0,
      "is_directory": true
    }
  ]
}
```

### file.stat
Request payload:

```json
{
  "path": "/config/config.json"
}
```

Response payload:

```json
{
  "path": "/config/config.json",
  "size": 412,
  "modified_ms": 1713976322000,
  "is_directory": false
}
```

### file.delete
Request payload:

```json
{
  "path": "/images/old.bmp"
}
```

Response payload:

```json
{
  "path": "/images/old.bmp",
  "deleted": true
}
```

### Path policy
- Allowed roots are `/config` and `/images` only.
- Paths outside allowed roots are rejected.
- Directory deletion is rejected by `file.delete`.

## Errors

error_code values currently include:
- invalid_json
- missing_field
- unknown_command
- validation_failed
- not_found
- restart_required
- internal_error
- queue_full
- timeout
- busy
- filesystem_unavailable
- filesystem_error
- file_not_found
- path_not_allowed

Clients should always branch on status and error_code, not only message text.
