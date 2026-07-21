### Обработка ключей запуска
Ключи `rpmbuild` обрабатываются через `popt`.

Основные точки:

- `build/poptBT.c` — таблица опций `rpmBuildPoptTable[]` и callback `buildArgCallback()`;
- `lib/rpmcli.h` — структура `rpmBuildArguments_s`;
- `rpmqv.c` — формирование маски стадий сборки `buildAmount`;
- `build.c` и `build/build.c` — передача флагов и запуск стадий сборки.

### Обработка секций `spec`
Ключевые файлы:

- `build/parseSpec.c` — распознавание секций `%prep`, `%build`, `%install`, `%check`, `%clean` и других;
- `build/parseBuildInstallClean.c` — накопление текста секций `%conf/%build/%install/%check/%clean`;
- `build/build.c` — выполнение секций через `doScript()`.

`doScript()` является  механизмом запуска shell-кода в процессе сборки.

## Что изменено

### 1. Добавлен новый ключ
В `build/poptBT.c` добавлен новый ключ:

- `--makecleanskiplist`
- сохраняется в `rpmBuildArguments_s`;
- выставляет новый build-флаг `RPMBUILD_MAKECLEANSKIPLIST`.

### 2. Расширены внутренние структуры
Добавлены:

- поле `makeCleanSkipList` в `lib/rpmcli.h`;
- флаг `RPMBUILD_MAKECLEANSKIPLIST` в `build/rpmbuild.h`.

### 3. Сохранение флага при построении `buildAmount`
В `build.c` добавлена защита, чтобы новый флаг не терялся при дальнейшей сборке маски стадий.

### 4. Запуск внешнего скрипта
В `build/build.c` в `buildSpec()` добавлен вызов внешнего скрипта через механизм:

- используется `doScript(..., RPMBUILD_STRINGBUF, ...)`;
- скрипт запускается **до `%prep`**;
- перед вызовом временно отключается `spec->buildSubdir`, чтобы избежать ошибки `cd` в еще не созданный каталог.

## Новый внешний скрипт
Добавлен файл:

- `rpm-build-9b20cf5/scripts/makecleanskiplist`

Скрипт:

- выводит свой полный путь;
- печатает все переменные окружения;
- пишет результат в лог сборки.

## Изменения в установке и документации

### Установка
В `scripts/Makefile.am` скрипт добавлен в:

- `EXTRA_DIST`
- `config_SCRIPTS`

Это позволяет включать его в пакет и устанавливать в каталог rpm helper-скриптов.

### Документация
В `doc/rpmbuild.8`:

- ключ `--makecleanskiplist` добавлен в synopsis;
- добавлено его краткое описание.

## Какие файлы изменены

- `rpm-build-9b20cf5/build/poptBT.c`
- `rpm-build-9b20cf5/lib/rpmcli.h`
- `rpm-build-9b20cf5/build/rpmbuild.h`
- `rpm-build-9b20cf5/build.c`
- `rpm-build-9b20cf5/build/build.c`
- `rpm-build-9b20cf5/scripts/Makefile.am`
- `rpm-build-9b20cf5/scripts/makecleanskiplist`
- `rpm-build-9b20cf5/doc/rpmbuild.8`
- `rpm-build-9b20cf5/build/test_makecleanskiplist`
- `rpm-build-9b20cf5/build/Makefile.am`

Также подготовлен патч:

- `rpm-build-makecleanskiplist.patch`

## Демо-тесты
Добавлен автотест `build/test_makecleanskiplist` (демо-версия) на базе существующего harness `test-functions.sh`.

Проверяет:
- вызов helper до `%prep` при `-bp` / `-bc`;
- вывод пути и окружения в лог;
- отсутствие вызова без флага и с `--nobuild`;
- ошибку сборки, если helper отсутствует.

Запуск после сборки `rpm-build`:
```bash
cd rpm-build-9b20cf5/build
./test_makecleanskiplist
```

## Как работает итоговая схема

1. Пользователь запускает `rpmbuild` с ключом `--makecleanskiplist`.
2. Ключ разбирается через `popt`.
3. В структуре аргументов и в маске стадий сохраняется новый флаг.
4. `buildSpec()` до `%prep` вызывает внешний helper-скрипт.
5. Скрипт печатает в лог:
   - свой путь;
   - переменные окружения.
6. Далее сборка продолжается в обычном режиме.

## Проверка
Подготовлено для проверки:

- патч `rpm-build-makecleanskiplist.patch` формируется через `git diff` относительно initial commit;
- helper-скрипт `scripts/makecleanskiplist` устанавливается в `${prefix}/lib/rpm/`;
- демо-тест `build/test_makecleanskiplist` можно запустить после сборки модифицированного `rpm-build`.

## Итог
Реализована  интеграция внешнего shell-скрипта в `rpm-build` по новому ключу `--makecleanskiplist`. Для внедрения использован механизм `doScript()`.
