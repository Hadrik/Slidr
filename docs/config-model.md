# Configuration Model

Top-level config shape:

```json
{
  "components": [
    {
      "id": "slider_0",
      "type": "slider",
      "options": {
        "pin": 1,
        "min_value": 0,
        "max_value": 100,
        "deadzone": 2,
        "adc_bits": 12
      }
    }
  ]
}
```

Rules:
- id is unique across all components.
- type must have a registered schema.
- options contains field/value pairs validated by schema.

Mutability classes:
- RuntimeMutable: applied immediately.
- RestartRequired: value is staged and component is marked pending restart.

Update modes:
- Full apply: config.apply_full with full document.
- Targeted patch: config.update_field for one field on one component id.

Current default schemas in runtime:
- slider:
  - pin: RestartRequired
  - adc_bits: RestartRequired
  - min_value: RuntimeMutable
  - max_value: RuntimeMutable
  - deadzone: RuntimeMutable
- led:
  - pin: RestartRequired
