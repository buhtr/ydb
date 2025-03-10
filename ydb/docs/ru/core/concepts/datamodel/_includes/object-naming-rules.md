## Правила наименования схемных объектов {#object-naming-rules}

У каждого [схемного объекта](../../../concepts/glossary.md#scheme-object) базы данных в {{ ydb-short-name }} есть имя. В YQL-выражениях имена схемных объектов указываются с помощью идентификаторов, заключённых в обратные кавычки (`` ` ``) или без этих символов. Для более подробной информации об идентификаторах, см. [{#T}](../../../yql/reference/syntax/lexer.md#keywords-and-ids).

Имена схемных объектов в {{ ydb-short-name }} должны соответствовать следующим требованиям:

- Имя объекта может состоять из следующих символов:
    - прописные латинские буквы;
    - строчные латинские буквы;
    - цифры;
    - специальные символы: `.`, `-` и `_`.
- Длина имени объекта не должна превышать 255 символов.
- Объекты не должны создаваться в папках, имена которых начинаются с точки, таких как `.sys`, `.medatata`, `.sys_health`.

## Правила наименования колонок {#column-naming-rules}

Имена колонок в {{ ydb-short-name }} должны соответствовать следующим требованиям:

- Имя колонки может состоять из следующих символов:
    - прописные латинские буквы;
    - строчные латинские буквы;
    - цифры;
    - специальные символы: `-` и `_`.
- Длина имени колонки не должна превышать 255 символов.
- Имя колонки не должно начинаться с системного префикса `__ydb_`.
