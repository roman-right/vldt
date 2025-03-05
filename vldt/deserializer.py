from datetime import datetime

GLOBAL_DESERIALIZER = {
    datetime: {
        str: lambda v: datetime.fromisoformat(v),
        int: lambda v: datetime.fromtimestamp(v),
    },
}
