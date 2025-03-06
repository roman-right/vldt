from typing import ClassVar

from vldt.models import DataModel


class ModelWithClassVars(DataModel):
    CLASS_VAR_1: ClassVar[int] = 0
    CLASS_VAR_2: ClassVar[str] = "default"

    id: int = 100

class TestClassVars:
    def test_class_vars(self):
        assert ModelWithClassVars.CLASS_VAR_1 == 0
        assert ModelWithClassVars.CLASS_VAR_2 == "default"
        assert ModelWithClassVars().id == 100
        assert ModelWithClassVars().CLASS_VAR_1 == 0
        assert ModelWithClassVars().CLASS_VAR_2 == "default"

        ModelWithClassVars.CLASS_VAR_1 = 10
        ModelWithClassVars.CLASS_VAR_2 = "changed"
        assert ModelWithClassVars.CLASS_VAR_1 == 10
        assert ModelWithClassVars.CLASS_VAR_2 == "changed"
        assert ModelWithClassVars().CLASS_VAR_1 == 10
        assert ModelWithClassVars().CLASS_VAR_2 == "changed"
        assert ModelWithClassVars(id=200).id == 200
        assert ModelWithClassVars(id=200).CLASS_VAR_1 == 10
        assert ModelWithClassVars(id=200).CLASS_VAR_2 == "changed"
        assert ModelWithClassVars().id == 100
        assert ModelWithClassVars().CLASS_VAR_1 == 10
        assert ModelWithClassVars().CLASS_VAR_2 == "changed"
        assert ModelWithClassVars(id=300).id == 300
        assert ModelWithClassVars(id=300).CLASS_VAR_1 == 10
        assert ModelWithClassVars(id=300).CLASS_VAR_2 == "changed"
        assert ModelWithClassVars().id == 100
        assert ModelWithClassVars().CLASS_VAR_1 == 10
        assert ModelWithClassVars().CLASS_VAR_2 == "changed"

        ModelWithClassVars.CLASS_VAR_1 = 0
        ModelWithClassVars.CLASS_VAR_2 = "default"
        assert ModelWithClassVars.CLASS_VAR_1 == 0
        assert ModelWithClassVars.CLASS_VAR_2 == "default"
        assert ModelWithClassVars().id == 100
        assert ModelWithClassVars().CLASS_VAR_1 == 0
        assert ModelWithClassVars().CLASS_VAR_2 == "default"
        assert ModelWithClassVars(id=400).id == 400
        assert ModelWithClassVars(id=400).CLASS_VAR_1 == 0
        assert ModelWithClassVars(id=400).CLASS_VAR_2 == "default"

        m = ModelWithClassVars()
        assert m.CLASS_VAR_1 == 0
        assert m.CLASS_VAR_2 == "default"

        ModelWithClassVars.CLASS_VAR_1 = 1
        ModelWithClassVars.CLASS_VAR_2 = "changed"

        assert m.CLASS_VAR_1 == 1
        assert m.CLASS_VAR_2 == "changed"

        assert ModelWithClassVars.CLASS_VAR_1 == 1
        assert ModelWithClassVars.CLASS_VAR_2 == "changed"

    def test_to_dict(self):
        m = ModelWithClassVars()
        assert m.to_dict() == {"id": 100}

    def test_to_json(self):
        m = ModelWithClassVars()
        assert m.to_json() == '{"id":100}'