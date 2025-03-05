import json


def type_error_to_dict(error: TypeError):
    """Converts a TypeError's value into a dictionary by interpreting it as JSON.

    Args:
        error (TypeError): The error instance containing a JSON-formatted string in its 'value' attribute.

    Returns:
        dict: The dictionary parsed from the JSON string.
    """
    return json.loads(str(error.value))
