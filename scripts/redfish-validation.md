# Redfish schema validation

WallaBMC's Redfish implementation can be checked against the published
Redfish schemas using the DMTF
[Redfish-Service-Validator](https://github.com/DMTF/Redfish-Service-Validator).
It crawls the service starting at `/redfish/v1` and validates every
resource it finds against the DMTF schemas
(<https://redfish.dmtf.org/redfish/schema_index>).

This complements `scripts/test-redfish.sh`, which checks specific
endpoints and behaviours: the validator checks broad schema
conformance, the test runner asserts our specific values.

## Install

The validator is a Python tool. Install it into a virtualenv, you probably
already have one for west. Either use that or create a new one, eg:

```bash
python3 -m venv scripts/.venv          # if it does not already exist
scripts/.venv/bin/pip install redfish_service_validator
```

## Run

The validator only issues GET requests, so it does not change the board
state (no power on/off).

```bash
scripts/.venv/bin/rf_service_validator \
    --rhost http://192.168.1.110 \
    --authtype Basic -u admin -p admin \
    --logdir $PWD/rsv-logs
```

- `--rhost` takes the address *with scheme*. Use `https://` to exercise
  the TLS service instead (WallaBMC uses a self-signed certificate).
- Schemas are fetched from <https://redfish.dmtf.org> at run time. To run
  offline, download the schema bundle and pass
  `--schema_directory <dir>`.
- Point `--rhost` at the QEMU forward (e.g. `http://127.0.0.1:8080`) to
  validate a QEMU build.

## Reading the results

The run prints a per-resource summary and a final table:

```
+--------------+--------------+--------------+--------------+
|     PASS     |     WARN     |     FAIL     |  NOT TESTED  |
+--------------+--------------+--------------+--------------+
|     142      |      1       |      1       |     344      |
+--------------+--------------+--------------+--------------+
```

WallaBMC currently has one known WARN (see OEM properties) and one known
FAIL (see Write-only password) below; anything beyond those is a
regression.

- **FAIL** — a schema violation that should be fixed. `rf_service_validator`
  exits non-zero when there are any failures.
- **WARN** — a recommendation, or something the validator could not
  fully check (see OEM properties below).
- **NOT TESTED** — optional properties that are absent, or schema
  branches the service does not exercise. These are expected, not
  problems.

Full reports (HTML, Excel and a debug log) are written under `--logdir`;
the debug log lists each `FAIL`/`WARN` with the offending property and
resource.

### OEM properties

WallaBMC exposes an OEM object under `Managers/bmc`:

```
WARN - /Oem/WallaBMC ([Object]): Schema Error: Unable to locate the
schema definition for the 'WallaBMC.v1_0_0.WallaBMC' type.
```

This is expected. Redfish requires an OEM object to carry an
`@odata.type`, but that type is a WallaBMC-specific extension with no
published schema, so the validator cannot resolve it. We deliberately do
not ship an OEM schema. Pass `--nooemcheck` to skip OEM validation and
silence this warning:

```bash
scripts/.venv/bin/rf_service_validator \
    --rhost http://192.168.1.110 \
    --authtype Basic -u admin -p admin \
    --nooemcheck --logdir $PWD/rsv-logs
```

### Write-only password

The admin account fails validation:

```
FAIL - /Password ([Empty String]): Null Error: The property 'Password'
is write-only and is expected to be null in responses.
```

`Password` is write-only, so Redfish requires it to be serialized as
`null` in GET responses. Zephyr's `json_obj_encode()` cannot emit `null`
for a string property — it emits an empty string (`""`) instead — so the
account resource reports `"Password": ""` and the validator flags it.

This is a known limitation of the JSON encoder, not a modelling mistake,
and is currently unfixed. Fixing it requires the encoder to emit `null`
for an unset string (or omitting the property entirely from responses).

## Related tools

- [Redfish-Protocol-Validator](https://github.com/DMTF/Redfish-Protocol-Validator)
  checks HTTP protocol behaviour (methods, status codes, headers). Note
  it performs write operations, so it is **not** read-only.
