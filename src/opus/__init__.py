"""Conversor Opus para Python."""
import subprocess
from pathlib import Path

__version__ = "0.1.0"
_EXECUTABLE = Path(__file__).resolve().parent / "bin" / "opus_tool"

def ejecutar():
    """Abre el menú interactivo de opus_tool."""
    if not _EXECUTABLE.is_file():
        raise FileNotFoundError(f"No se encuentra {_EXECUTABLE}")
    return subprocess.run([str(_EXECUTABLE)], check=False)

def convertir(entrada=None, salida=None, modo=None):
    """Convierte archivos usando opus_tool."""
    if entrada is None or salida is None or modo is None:
        return ejecutar()

    modos = {"pcm-oraw": "1", "oraw-opus": "2", "pcm-opus": "3"}
    if modo not in modos:
        raise ValueError("Modos: pcm-oraw, oraw-opus, pcm-opus")

    proceso = subprocess.run(
        [str(_EXECUTABLE)],
        input=f"{modos[modo]}\n{Path(entrada).resolve()}\n{Path(salida).resolve()}\n",
        text=True,
        capture_output=True,
        check=False,
    )
    if proceso.stdout:
        print(proceso.stdout, end="")
    if proceso.stderr:
        print(proceso.stderr, end="")
    if proceso.returncode:
        raise RuntimeError(f"opus_tool terminó con código {proceso.returncode}")
    return proceso
