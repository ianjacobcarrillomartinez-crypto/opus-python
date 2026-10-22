# opus

Paquete Python que incluye el ejecutable opus_tool.

## Uso

```python
import opus
print(opus.__version__)
opus.ejecutar()






EOF

EOX
EOF
EOF
# opus-python
# opus-python


## Android / Termux

The installed `opus_tool` executable currently requires a manual permission fix:

```bash
chmod +x "$(python -c 'import importlib.resources; print(importlib.resources.files("opus").joinpath("bin/opus_tool"))')"
```

Known issue: the wheel installation does not preserve the executable permission.
