# Control de Acceso - Servidor FastAPI

## Requisitos
- Python 3.11+
- Oracle DB corriendo en localhost:1521

## Instalación (solo la primera vez)

```bash
# 1. Crear entorno virtual
python -m venv venv

# 2. Activarlo
#    Windows:
venv\Scripts\activate
#    Mac/Linux:
source venv/bin/activate

# 3. Instalar dependencias
pip install -r requirements.txt
```

## Configuración
Edita en `main.py` las líneas:
```python
DB_USER     = "ControlAcceso"
DB_PASSWORD = "admin"
DB_DSN      = "localhost:1521/XE"   # cambia XE si tu service name es diferente
```

Para saber tu service name en Oracle:
```sql
SELECT value FROM v$parameter WHERE name = 'service_names';
```

## Correr el servidor

```bash
python main.py
```

El servidor arranca en: http://0.0.0.0:8000

## Endpoints principales

| Método | Ruta                    | Descripción                          |
|--------|-------------------------|--------------------------------------|
| GET    | /ping                   | Health check (ESP32 lo usa primero)  |
| POST   | /asistencia             | Registrar una asistencia             |
| POST   | /sync                   | Sincronizar cola completa del ESP32  |
| POST   | /alumno                 | Registrar alumno                     |
| POST   | /profesor               | Registrar profesor                   |
| POST   | /materia                | Registrar materia                    |
| POST   | /horario                | Registrar horario                    |
| POST   | /acceso_denegado        | Registrar acceso denegado            |
| POST   | /notificacion           | Registrar notificación               |
| GET    | /alumnos                | Listar alumnos                       |
| GET    | /asistencias            | Listar asistencias                   |
| GET    | /notificaciones         | Listar notificaciones no leídas      |


