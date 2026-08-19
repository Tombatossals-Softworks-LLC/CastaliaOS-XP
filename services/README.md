# services/ — runit service definitions

System services per Bible §6.4: each service is a directory with a runit
`run` script (auditable in one read), an optional `log/run` (svlogd, plain
text — §6.14), and a `service.conf` metadata file that the **Services
Manager** app reads to present the service in plain language.

## Layout

```
services/<name>/run            # exec the daemon (runit supervises)
services/<name>/log/run        # exec svlogd -tt /var/log/castalia/<name>
services/<name>/service.conf   # [service] name/description/category
```

## service.conf format

```ini
[service]
name = LightDM
description = Pantalla de acceso (greeter)
category = system        # system | network | hardware | optional
essential = true         # Services Manager warns before stopping
```

The installer enables the edition's service set; `castalia-hwprobe`,
networking, cups etc. land here as their phases arrive (§18).
