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

### file.upload.start
Request payload:

```json
{
  "path": "/images/new.bmp",
  "total_size": 1240,
  "chunk_size": 768
}
```

`chunk_size` is optional. The device clamps to its max chunk size.

Response payload:

```json
{
  "session_id": "upload_1713976322000",
  "chunk_size": 768,
  "total_size": 1240,
  "expected_chunks": 2
}
```

### file.upload.chunk
Request payload:

```json
{
  "session_id": "upload_1713976322000",
  "index": 0,
  "data_base64": "AAECAw==",
  "chunk_crc32": 305419896
}
```

Response payload:

```json
{
  "bytes_received": 768,
  "total_size": 1240,
  "next_index": 1
}
```

### file.upload.finish
Request payload:

```json
{
  "session_id": "upload_1713976322000",
  "total_crc32": 1234567890
}
```

`total_crc32` is optional. When omitted (0), the device skips total CRC validation.

Response payload:

```json
{
  "session_id": "upload_1713976322000",
  "completed": true
}
```

### file.upload.cancel
Request payload:

```json
{
  "session_id": "upload_1713976322000"
}
```

Response payload:

```json
{
  "session_id": "upload_1713976322000",
  "cancelled": true
}
```

### file.download.start
Request payload:

```json
{
  "path": "/images/new.bmp",
  "chunk_size": 768
}
```

`chunk_size` is optional. The device clamps to its max chunk size.

Response payload:

```json
{
  "session_id": "download_1713976322000",
  "chunk_size": 768,
  "total_size": 1240,
  "total_chunks": 2,
  "total_crc32": 987654321
}
```

### file.download.chunk
Request payload:

```json
{
  "session_id": "download_1713976322000",
  "index": 0
}
```

Response payload:

```json
{
  "session_id": "download_1713976322000",
  "index": 0,
  "chunk_size": 768,
  "chunk_crc32": 305419896,
  "is_last": false,
  "data_base64": "AAECAw=="
}
```

### file.download.finish
Request payload:

```json
{
  "session_id": "download_1713976322000"
}
```

Response payload:

```json
{
  "session_id": "download_1713976322000",
  "completed": true
}
```

## Events

### file.transfer.timeout
One-shot async event emitted when an active transfer exceeds the timeout window.

```json
{
  "protocol_version": "1.0",
  "kind": "file.transfer.timeout",
  "message_id": "event_1713976322000",
  "timestamp_ms": 1713976322000,
  "status": "error",
  "error_code": "timeout",
  "payload": {
    "session_id": "upload_1713976322000",
    "path": "/images/new.bmp",
    "bytes_transferred": 384,
    "total_size": 1240,
    "direction": "upload",
    "reason": "timeout"
  }
}
```
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
- transfer_session_not_found
- transfer_crc_mismatch
- transfer_out_of_order
- transfer_size_mismatch
- transfer_decode_failed

Clients should always branch on status and error_code, not only message text.
