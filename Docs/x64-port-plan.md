# X-Ray 1.0007rc1 → x64 без мультиплеера: план по этапам

Статус: план от 2026-09-28. Репозиторий: `K:\Vibe Projects\stalker\X-Ray`, ветка `main`, origin — `github.com/xrModder/X-Ray`.

## 1. Итог анализа

### 1.1. Исходное состояние

- `X-Ray.sln`: 22 проекта, только платформа `Win32`. Конфигурации: `Debug`, `Mixed`, `Release` и `Debug_Dedicated`, `Mixed_Dedicated`, `Release_Dedicated`. Toolset `v143`, CRT `/MD`.
- На машине есть VS 2026 Community с toolset v143 (14.39, библиотеки x86 и x64) и Windows SDK 10.0.26100 (x64-версии `d3d9.lib`, `dinput8.lib`, `dsound.lib`, `dxguid.lib`).
- `X-Ray.props`: `TargetMachine=MachineX86`, `LargeAddressAware=true`, `OutDir=Output\Binaries\` без разделения по платформам.
- Объём кода: xrGame ≈ 383 тыс. строк, xrCore ≈ 48 тыс., рендеры ≈ 35 тыс.
- Мультиплеер: ≈ 132 файла и 25 тыс. строк в xrGame (режимы DM/TDM/AH, MP-интерфейс, обёртки GameSpy, BattlEye), GameSpy SDK (`xrGameSpy`, 99 файлов, 34 тыс. строк C), транспорт DirectPlay в `xrNetServer`, `xrD3D9-Null` для dedicated. Кроме того, ≈ 130 ссылок на `g_dedicated_server` и ≈ 320 проверок типа игры (`IsGameTypeSingle`, `GameID()`).
- Одиночная игра уже работает через in-process «direct connect»: `xrNetServer/NET_Server.cpp:298` включает `psNET_direct_connect` для `/single`. DirectPlay в одиночной игре не используется, поэтому его удаление не затрагивает SP.
- Git: две правки пользователя (`Device_Initialize.cpp` — заголовок окна, `console_commands.cpp` — команда `fov`) были не закоммичены; закоммичены на этапе 0.
- `AGENTS.md` указывал пути `K:\stalker\...`, которых нет; исправлено на этапе 0 (корень — `K:\Vibe Projects\stalker`).

### 1.2. Что блокирует x64

| Область | Что найдено в коде | Решение | Этап |
|---|---|---|---|
| Lua | LuaJIT 1.0.3 на базе «Lua 5.1 (alpha)». JIT только для x86 (`ljit_x86.h`, DynASM). Корутины Coco (`lcoco`, `lua_newcthread`) | LuaJIT 2.1 (GC64 на x64) в виде `lua51.dll`, отказ от Coco | 3 |
| Звук | Creative OpenAL router (`Externals/OpenAL32`) загружает `wrap_oal.dll`/`*oal.dll`. DirectSound-бэкенд использует `eax.lib`/`EAX.dll` (готовые x86-бинарники). По логу EAX и сейчас «absent» | OpenAL Soft; удалить DirectSound-бэкенд и `EAX.dll` | 3 |
| DirectX | `Externals/DXSDK_Jun2010/Lib` содержит только x86-библиотеки. `dxerr.lib` | Разделить на `Lib/x86` и `Lib/x64`; `dxerr` заменить своим форматированием `HRESULT` | 2, 3 |
| Сеть | DirectPlay8 (`dplay8.h`, `dpaddr.h`, `dxguid.lib` из `DXSDK_Aug2007`) | Удалить, оставить только in-process direct connect | 1 |
| GameSpy | `xrGameSpy` (asm в `gsLargeInt.c`, `ghttpEncryption.c`) и обёртки в `xrGame/gamespy` | Удалить | 1 |
| xrCPU_Pipe | x87/MMX/3DNow!/SSE inline asm: `xrM44mul.cpp`, `xrTransfer.cpp`, `xrMemCopy8_3DNow!.cpp`, `xrMemFill32_3DNow!.cpp`, `*_3DNow!.cpp`, `*_SSE.cpp` | C++-реализации. `skin1W`/`skin2W` на C++ уже есть | 2 |
| xrCore | `_control87(_PC_24, MCW_PC)`, `fstcw`/`fldcw`, `rdtsc` и `cpuid` через asm, asm в `xrMemory_pso_*`, `__asm int 3`, стек в `blackbox/BlackBoxUI.cpp` через `Eip/Ebp/Esp` | Интринсики `__cpuid`, `__rdtsc`, `__debugbreak`; `_controlfp_s`; `RtlCaptureContext` + `StackWalk64` | 2 |
| xrCDB | asm в `OPC_FPU.h`, `OPC_MemoryMacros.h` | `std::min`, `std::max`, `memset`, `memcpy` | 2 |
| Видео | `theora_static` собирается с `OC_X86_ASM` (MSVC MMX). `xrTheora_Surface_mmx.cpp` — мёртвый код (`#undef MMX_TV_YUV2ARGB` в `xrTheora_Surface.cpp`) | x64 без `OC_X86_ASM`; mmx-файл исключить из сборки | 2, 4 |
| Защита | `SECUROM_MARKER_*` (asm `_emit`) в `x_ray.cpp` (`IsOutOfVirtualMemory`); `CheckCopyProtection()` вызывается в 5 местах `x_ray.cpp`, но `USE_COPYPROTECTION` выключен, функция пустая | Удалить маркеры, вызовы и оба заголовка | 2 |
| Указатели | Единицы-десятки явных `(u32)ptr`, `*(u32*)&x`, `u32(this)` | `/we4311 /we4312 /we4302` и резерв нижних 4 ГБ | 5, 6 |
| Сейвы | `CSaver` пишет POD как `stream.w(&data, sizeof(T))` (`xrGame/object_saver.h:20`). Совместимость сейвов Win32 ↔ x64 не требуется | Проверяется только сохранение и загрузка внутри x64 | 6 |

### 1.3. Что не является проблемой

- Форматы уровней и графов не содержат указателей. `CDB::TRI` занимает 16 байт, поле `dummy` хранит индекс и флаги. `CGameGraph::CVertex` хранит смещения (`dwEdgeOffset`), а не указатели.
- Windows x64 использует модель LLP64: `long` и `DWORD` остаются 32-битными. Меняются только указатели, `size_t` и `ptrdiff_t`. Массовая замена `u32` на `u64` не нужна и сломает форматы.
- В xrCore уже есть ветки `_M_AMD64` (`_math.cpp`, `cpuid.cpp`, `xrDebug*.cpp`, `xrMemory_pso_*`). Но `cpuid.cpp` на AMD64 — заглушка с жёстко прописанным `AuthenticAMD`.
- ODE уже использует `uintptr_t` для выравнивания. В ODE и luabind явных кастов указатель → int не найдено. BugTrap содержит ветки `_M_X64`.
- Аллокатор xrMemory выдаёт память с выравниванием 16 байт. Запасной путь уже есть: ключ `-pure_alloc` (CRT `malloc`).
- Python в проекте нет.

### 1.4. Кодировка исходников (исправлено в срезе 1.0)

- Было: 558 файлов вне `Externals` (≈ 510 в xrGame) не в UTF-8. Из них 549 в Windows-1251 и 9 в Windows-1252: английские комментарии с типографскими кавычками, `–`, `©`, `™`. Инструменты правки агентов рассчитаны на UTF-8: Edit в Claude Code заменил такие байты на `U+FFFD` во всём файле (проверено на `x_ray.cpp` на этапе 0).
- Кириллица вне комментариев была только в идентификаторах: `IPureLоadableObject` (русская «о», 4 файла) и `seсt_name` (русская «с», `ui/UIMainIngameWnd.h`), плюс текст в `#pragma todo` (`Level_Bullet_Manager.cpp`). Строковых литералов с кириллицей нет, поэтому перевод в UTF-8 не меняет поведение игры.
- Стало: идентификаторы переименованы в ASCII; 554 файла с не-ASCII символами переведены в UTF-8 с BOM (MSVC по BOM читает их как UTF-8); остальные файлы — чистый ASCII. Edit сохраняет BOM, CRLF и кириллицу (проверено).

## 2. Отличия от исходного плана

1. Сначала Win32, потом x64. Удаление мультиплеера и замена x86-кода на переносимый делаются на рабочей Win32-сборке. Каждое изменение проверяется на игре, которая уже запускается. После этого переход на x64 содержит только настоящие 64-битные проблемы: усечение указателей, размеры структур, x64-библиотеки.
2. Мультиплеер удаляется до порта. Нет смысла портировать ≈ 65 тыс. строк (GameSpy, DirectPlay, MP-режимы, dedicated), которые потом будут удалены. GameSpy и DirectPlay сами содержат x86-зависимости.
3. Главный блокер отсутствовал в исходном плане: LuaJIT 1.0.3 с Coco работает только на x86. Замена на LuaJIT 2.1 — самый рискованный шаг. Он делается на Win32, чтобы отделить ошибки Lua от ошибок x64.
4. Звук переводится на OpenAL Soft (есть x86 и x64). Готовые x86-бинарники EAX удаляются.
5. Win32 остаётся собираемым и запускаемым до паритета x64 (требование AGENTS.md о запускаемой сборке после каждой задачи). x64 деплоится в отдельную папку `binaries_x64`.
6. «Путь B» (перенос SoC на OpenXRay) отклонён. OpenXRay основан на CoP 1.6.02, SoC там поддерживается не полностью, перенос геймплея — отдельный большой проект. OpenXRay используется только как справочник для конкретных решений: LuaJIT 2.1 с luabind, отказ от asm в xrCPU_Pipe, x64-правки.
7. Совместимость сейвов Win32 ↔ x64 и совместимость со сторонними модами не требуются (решение от 2026-09-28). Для x64 достаточно, чтобы сейв, сделанный в x64, загружался в x64.

## 3. Правила выполнения

- Один срез — одна ветка. После среза: сборка `Release|Win32` (с этапа 4 — ещё и x64), деплой, `smoke.ps1`, ручной чек-лист.
- Срез закончен, когда выполнена его приёмка. Всё, что не нужно для приёмки, не делается.
- Форматы данных не меняются. Поля `u32` в файлах, сейвах и `NET_Packet` остаются `u32`. `uintptr_t` и `size_t` — только там, где значение является адресом или размером в памяти.
- Код без комментариев (AGENTS.md, Zero Comments Policy).
- Изменения данных игры — только через оверрайды в `gamedata\`. Архивы `gamedata.db*` не меняются.

Не цели:

- новый рендер (DX10/11/12) и новые графические функции;
- слияние с OpenXRay или CoP, переход на CMake, модернизация C++ без необходимости, замена luabind или ODE;
- чистка всех ≈ 320 проверок типа игры: удаляются только ветки, которые ссылаются на удалённый код;
- изменение форматов `all.spawn`, `level.*`, `.ogf`, сейвов;
- совместимость сейвов Win32 ↔ x64 и совместимость со сторонними модами;
- SDK и компиляторы уровней (их нет в репозитории), Linux.

## 4. Этапы

### Этап 0. Базовая линия и страховка — S (выполнен 2026-09-28)

Цель: точка отката и автоматическая проверка «игра загружает уровень».

Что сделано:

1. Правки пользователя (заголовок окна, `fov`) закоммичены в ветке `stage-0`, на ней стоит тег `soc-1.0007-win32-baseline`. Собственный remote не добавлен: нужен URL репозитория пользователя.
2. `AGENTS.md`: исправлены пути, добавлены команда сборки, запуск `smoke.ps1` и правило кодировки.
3. Полная пересборка `Release|Win32` из командной строки проходит: 22 проекта, 0 ошибок.
4. Движок получил тестовый ключ `-smoke_test <сек>` (`xrEngine/x_ray.cpp`, класс `CSmokeTest`; `xrEngine/device.cpp`, `OnWM_Activate`). После загрузки уровня и прекеша движок делает окно активным, игнорирует потерю фокуса, считает кадры N секунд, пишет в лог `* smoke_test: <кадры> frames in <мс> ms, <fps> fps, <n> inactive frames` и выполняет `quit`. Без ключа поведение игры не меняется. Причины: лог попадает на диск только при выходе или падении; окно игнорирует `WM_CLOSE`; без фокуса движок не рендерит и ставит игру на паузу.
5. `tools/smoke.ps1`:
   - собирает `appdata\smoke.ltx` из `user.ltx` с заменой `renderer`, `rs_fullscreen off`, `vid_mode 1280x720`, `snd_volume_eff 0`, `snd_volume_music 0`; `user.ltx` не меняется;
   - запускает `binaries\xrEngine.exe -nointro -silent_error_mode -ltx smoke.ltx -smoke_test <сек> -start server(<save>/single/alife/load) client(localhost)`;
   - PASS: процесс завершился сам, в логе есть строка `smoke_test`, нет `FATAL ERROR` и `stack trace`, при `-Baseline` нет новых строк `!`;
   - копия лога — `Output\Smoke\<время>-<рендер>.log`, найденные минидампы выводятся строкой `report`.
6. Эталоны: `appdata\savedgames\smoke_reference.sav` (копия `all.sav` от 2026-09-27); `Output\Smoke\baseline-r1.log` (37 строк `!`) и `baseline-r2.log` (45 строк `!`).

Особенности движка, найденные на этапе:

- `-load <save>` из командной строки не работает: команда `load` требует уже запущенной A-Life. Нужен `-start server(<save>/single/alife/load)`.
- `-start` читает всю строку после себя, поэтому он должен быть последним ключом.
- Ключи не должны содержать подстроку `-i`: по ней движок отключает захват ввода (`InitInput`).
- `-silent_error_mode` убирает диалог BugTrap: при падении лог сбрасывается, пишется минидамп, процесс завершается.
- Сейв, которого нет, вызывает `FATAL ERROR` в `CALifeUpdateManager::load`. На этом `smoke.ps1` падает за 8 секунд.

FPS: разброс между прогонами подряд большой (R1 714–1142, R2 443–735 при 1280×720), когда пользователь параллельно работает за машиной. Сравнение производительности — медиана трёх прогонов на свободной машине, допуск ±10 %.

Ручной чек-лист (для этапов, где smoke недостаточно): главное меню; новая игра → Кордон; диалог и торговля у Сидоровича; PDA; сохранение и загрузка; переход на другой уровень; видео-интро; звук и музыка; R1 и R2.

Приёмка (выполнена): тег есть; `smoke.ps1` проходит на R1 и R2 и повторно без новых строк `!`; на несуществующем сейве падает.

### Этап 1. Удаление мультиплеера, GameSpy и dedicated (Win32) — L

Цель: одиночная игра без MP-кода и сетевых зависимостей.

После каждого среза: сборка, деплой, `smoke.ps1 -Baseline` для R1 и R2, коммит.

- **1.0 Кодировка (выполнен 2026-09-28).** Файлы движка вне `Externals` переведены из Windows-1251/1252 в UTF-8 с BOM (раздел 1.4). Кодировку определял по доле букв; 9 файлов, которые эвристика приняла за CP866, оказались Windows-1252. Идентификаторы с кириллицей переименованы в ASCII. В `git diff` только перекодировка и эти переименования; сборка и `smoke.ps1` проходят. В `AGENTS.md` правило о побайтовой правке заменено правилом про UTF-8 с BOM.
- **1.1 Dedicated server (выполнен 2026-09-28).**
  - Удалены конфигурации `*_Dedicated` (`X-Ray.sln`, `xrEngine.vcxproj`, `X-Ray.props`), проект `xrD3D9-Null`, блоки `DEDICATED_SERVER` (14 файлов xrEngine).
  - Удалён флаг `g_dedicated_server`: ≈ 118 мест, в обычной сборке он всегда был `false`.
  - Удалены текстовая консоль `CTextConsole`, команды `net_dedicated_sleep`, `sv_dedicated_server_update_rate` и `sv_console_update_rate`, параметр `Dedicated` у `IPureServer` и параметр `dedicated` у `xrDebug::_initialize`.
  - `mm_mm_net_srv_dedicated` — настройка MP-меню, удалена вместе с MP-меню.
- **Эталоны smoke при удалении консольных команд.** `user.ltx` хранит значения всех команд, поэтому удалённая команда даёт в логе `! Unknown command: <имя>`. Это ожидаемо. После среза сравнить новые строки `!` со списком удалённых команд и, если лишнего нет, обновить `Output\Smoke\baseline-r1.log` и `baseline-r2.log`. Строки исчезнут сами, когда игра перезапишет `user.ltx` при обычном выходе.
- **1.2 + 1.3 + 1.5 Мультиплеер (выполнены одним срезом 2026-09-28).** Срезы 1.2, 1.3 и 1.5 зависят друг от друга: MP-режимы вызывают GameSpy, а скрипты меню вызывают удалённый Lua-экспорт. Поэтому они сделаны одним коммитом.
  - Удалены проект `xrGameSpy`, папка `xrGame/gamespy`, BattlEye, CD-key, патчер, браузер серверов, MP-режимы (`game_*_mp*`, deathmatch, team deathmatch, artefact hunt), `actor_mp_*`, `CSpectator`, `CMPPlayersBag`, статистика оружия, MP-UI (покупка, статистика, фраги, голосование, чат, скины, спавн, деньги, ранги, карты), `console_commands_mp.cpp`. Всего ≈ 290 файлов.
  - Фабрика объектов регистрирует только одиночные `game_sv_Single`, `game_cl_Single`, `CUIGameSP`. Шаблон `CObjectItemClientServerSingleMp` удалён.
  - `CMainMenu`: нет GameSpy, патчера и CD-key. `GetGSVer()` возвращает `1.0007(rc1)`: это значение раньше отдавал `xrGameSpy.dll`. Остались диалоги ошибок подключения и загрузки.
  - Удалены `CServerInfo` и `IGame_Level::GetLevelInfo`: их вызывал только выделенный сервер. Удалены `M_BATTLEYE`, `MSYS_CONFIG::is_battleye`, `g_dwEventDelay`.
  - `get_rank` перенесён в `ai_stalker_alife.cpp`: ИИ сталкеров в одиночной игре сравнивает оружие по рангам из `config\mp\mp_ranks.ltx`. Поэтому MP-конфиги в архивах нужны.
  - Gamedata-оверрайды (копия в репозитории — `Game\gamedata`): главное меню без кнопки сетевой игры (`ui_main_menu.script`, `ui_mm_main.xml`); опции без патчера и кнопки обновлений (`ui_mm_opt_main.script`, `ui_mm_opt_gameplay.script`, `ui_mm_opt.xml`); `script.ltx`, `game_registrator.script`, `ui_registrator.script` знают только тип `single`.
  - `mods\mp_military_2.xdb0` не удалён. Это файл игры, а папку `mods` монтирует общий механизм `CLocatorAPI::ProcessExternalArch`. Одиночной игре он не мешает.
  - Остались ветки `GameID() != GAME_SINGLE` и `IsGameTypeSingle()` в игровом коде (Actor, оружие, HUD, UI). В одиночной игре они не выполняются. Их удаление — отдельная чистка, в x64-переносе не нужна.
  - Проверка: 74 новые строки `! Unknown command` — только удалённые MP-команды; эталоны smoke обновлены. Диалог опций проверен отдельной временной пробой в меню: он создаётся без ошибок Lua.
- **1.4 Сетевой транспорт.** В `xrNetServer` оставить только in-process direct connect. Удалить DirectPlay, `Externals/DXSDK_Aug2007`, `NET_Compressor` (работает только при `!psNET_direct_connect`), сам флаг `psNET_direct_connect` и ключ `-no_direct_connect`. Убрать подключение dplay из `xrEngine/stdafx.h`.

Не трогать: классы `CSE_*` (от них зависят `all.spawn` и сейвы); `xrServer`, `xrClient`, `NET_Packet` (одиночная игра работает через них); базовые `game_cl_GameState` и `game_sv_GameState`.

Приёмка:

- в solution нет `xrGameSpy`, `xrD3D9-Null`, `*_Dedicated`; папки `Externals/DXSDK_Aug2007` нет;
- `dumpbin /dependents` для `xrEngine.exe`, `xrGame.dll`, `xrNetServer.dll` не показывает `xrGameSpy.dll`; в коде нет `IDirectPlay8`;
- чек-лист этапа 0 проходит, reference save загружается, в логе нет новых ошибок относительно эталона.

Риск: скрипты главного меню или регистрации классов ссылаются на удалённые классы, и игра падает при старте. Защита: gamedata-оверрайды того же среза и `smoke.ps1` после каждого среза.

### Этап 2. Переносимый код вместо x86-специфики (Win32) — M

Цель: в компилируемом коде движка нет inline asm и x86-only API. Поведение Win32 не меняется.

- xrCore:
  - `cpuid.cpp` → `__cpuid`/`__cpuidex` для обеих архитектур, AMD64-заглушку убрать;
  - `_math.cpp`, `_math.h`: `rdtsc` → `__rdtsc()`, `fstcw`/`fldcw` → `_controlfp_s`. Точность FPU (`_MCW_PC`, `_PC_24`) — только под `_M_IX86`. На x64 управляется только режим округления;
  - `__asm int 3` → `__debugbreak()` в `xrDebug.cpp`, `xrDebugNew.cpp`;
  - `xrMemory_pso_Copy.cpp`, `xrMemory_pso_Fill32.cpp` → `memcpy`, `std::fill_n` (существующая AMD64-ветка становится единственной);
  - `blackbox/BlackBoxUI.cpp`: `RtlCaptureContext` + `StackWalk64`, тип машины `IMAGE_FILE_MACHINE_I386` или `IMAGE_FILE_MACHINE_AMD64` по архитектуре.
- xrCDB: `OPC_FPU.h`, `OPC_MemoryMacros.h` → стандартные функции.
- xrCPU_Pipe: удалить 3DNow!-, SSE-asm- и x87-варианты. `m44_mul`, `transfer`, `memCopy`, `memFill32` — на C++. Интерфейс `xrBind_PSGP` и DLL сохранить.
- xrEngine: исключить из сборки `xrTheora_Surface_mmx.cpp`. В `x_ray.cpp` удалить `SECUROM_MARKER_*` (в `IsOutOfVirtualMemory`) и 5 вызовов пустой `CheckCopyProtection()`, затем `securom_api.h` и `CopyProtection.h`.
- DxErr: заменить `DXGetErrorString`/`DXTrace` в `xrCore/xrDebug*.cpp` и `xrEngine/Device_Misc.cpp` своим форматированием `HRESULT` (`FormatMessage` и hex-код). Удалить `dxerr.lib`: его x64-версия из SDK 2010 не линкуется с современным CRT без `legacy_stdio_definitions.lib`.

Приёмка:

- поиск `\b__?asm\b` по компилируемым файлам `Sources` (кроме `Externals` и `xrLua/LuaJIT`, который заменяется на этапе 3) даёт 0;
- `smoke.ps1 -Baseline` проходит на R1 и R2; медиана FPS трёх прогонов на свободной машине в пределах ±10 % от эталона, снятого так же;
- при искусственном падении в `Mixed` лог содержит стек (временная правка, не коммитится).

### Этап 3. Замена x86-only зависимостей (Win32) — L

#### 3.1. LuaJIT 1.0.3 → LuaJIT 2.1

- Исходники LuaJIT 2.1 положить в `Externals/LuaJIT` (зафиксированный коммит). Собирать `msvcbuild.bat` из Makefile-проекта в solution; результат — `lua51.dll` и `lua51.lib` для Win32 и x64. Если Makefile-проект окажется ненадёжным, собрать один раз из x86/x64 Native Tools Command Prompt и закоммитить бинарники.
- Для x64 обязателен режим GC64: движок создаёт state через `lua_newstate(lua_alloc_xr, …)`, а без GC64 на x64 это не работает.
- `xrLua.dll` содержит только luabind и линкуется с `lua51.lib`.
- Правки движка:
  - `xrGame/script_engine.h:19` — убрать `lcoco.h`; `xrGame/script_storage.cpp:142` — убрать `luaopen_coco`;
  - `xrGame/script_thread.cpp:62` — `lua_newcthread` → `lua_newthread`;
  - `xrGame/script_thread.cpp:11` и `:155` — убрать `lstate.h`, `lua()->status` → `lua_status(lua())`;
  - `-nojit` → `luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`;
  - проверить `luaJIT_setmode` в `xrEngine/ResourceManager_Scripting.cpp:230` (shader-скрипты R1/R2).
- Совместимость скриптов SoC (они писались под Lua 5.1 alpha): в распакованных скриптах найти `string.gfind`, `math.mod`, `table.getn`, `table.setn`, `coroutine.yield`, `coco.`, `jit.`. Для реально используемых функций, которых нет в LuaJIT 2.1, добавить алиасы при инициализации script engine.
- Риск Coco: без него `yield` через границу C (luabind-вызов → Lua → `yield`) даёт ошибку `attempt to yield across C-call boundary`. Проверить скрипты, которые вызывают `coroutine.yield` или `wait()`.

Приёмка: `smoke.ps1 -Baseline` на R1 и R2; вручную: новая игра, первые диалоги, получение и сдача квеста, торговля, PDA, 15+ минут игры с A-Life — без Lua-ошибок в логе; `-nojit` работает; медиана FPS не хуже эталона более чем на 10 %.

#### 3.2. Звук: OpenAL Soft

- Удалить `Externals/OpenAL32`, `Externals/EAX`, `Game/binaries/EAX.dll`.
- Удалить DirectSound-бэкенд `SoundRender_CoreD.*` и `SoundRender_TargetD.*`. Только он использует `eax.lib` (`xrSound/stdafx.cpp:18`) и `EAXDirectSoundCreate` (`SoundRender_CoreD.cpp:63`).
- Подключить OpenAL Soft фиксированной версии (x86 и x64): заголовки, `OpenAL32.lib`, рантайм `soft_oal.dll`, переименованный в `OpenAL32.dll`. OpenAL Soft с версии 1.23 эмулирует EAX, путь `EAXSet`/`EAXGet` через `alGetProcAddress` в `SoundRender_CoreA.cpp` сохраняется.

Приёмка: в логе устройство OpenAL Soft; 3D-позиционирование, музыка, эмбиент, диалоги работают; `snd_efx` вкл/выкл без падений.

#### 3.3. Библиотеки DirectX

- `Externals/DXSDK_Jun2010/Lib` разделить на `Lib/x86` (текущие файлы) и `Lib/x64`. Для x64 нужен только `d3dx9.lib` (NuGet `Microsoft.DXSDK.D3DX` или DirectX SDK June 2010, `Lib\x64`). `d3d9.lib`, `dinput8.lib`, `dsound.lib`, `dxguid.lib` для x64 берутся из Windows SDK. `LibraryPath` зависит от платформы. Заголовки не меняются.

Приёмка: Win32 собирается и проходит `smoke.ps1` без изменений поведения.

### Этап 4. Инфраструктура x64 — S

- `X-Ray.sln` и все `.vcxproj`: платформа `x64`, конфигурации `Mixed|x64` (оптимизация и `DEBUG`-ассерты — основная для порта) и `Release|x64`. `Debug|x64` на старте не нужен.
- `X-Ray.props`:
  - `TargetMachine` по платформе;
  - выходные папки x64: `Output\Binaries_x64\`, `Output\Libraries_x64\`, `Output\Intermediate_x64\$(ProjectName)\`. Пути Win32 не меняются, поэтому деплой из AGENTS.md продолжает работать;
  - для x64: `/we4311 /we4312 /we4302` (усечение указателя — ошибка компиляции).
- `theora_static` для x64 — без `OC_X86_ASM`.
- x64-конфигурации для `BugTrap`, `ODE`, `zlib`, `ogg`, `vorbis`, `theora`, `LuaJIT`.
- Папка игры: `binaries_x64\` и `Launch_x64.cmd` (`@start binaries_x64\xrEngine.exe`); в репозитории — в `Game\`. У `smoke.ps1` появляется параметр платформы (`binaries` или `binaries_x64`).

Приёмка: внешние библиотеки собираются под x64 (`dumpbin /headers` → `8664 machine (x64)`); Win32 собирается и проходит `smoke.ps1`.

### Этап 5. Сборка движка под x64 — M–L

Порядок по зависимостям solution: `xrCore` → `xrCDB`, `xrLua`, `xrParticles` → `xrSound`, `xrNetServer` → `xrEngine` → `xrCPU_Pipe` → `xrRender_R1`, `xrRender_R2` → `xrGame`.

Правила:

- C4311, C4312, C4302 исправлять по смыслу: адрес — указатель или `uintptr_t`; размер в памяти — `size_t`; разность указателей — `ptrdiff_t`. Поля форматов и протокола остаются `u32`, сужение делается явно в точке записи.
- C4267 и C4244 (`size_t` → `u32`) не исправлять массово. Разбирать только места, где значение — адрес, смещение в памяти или размер больше 4 ГБ.
- Вручную просмотреть то, что компилятор не ловит: `*(u32*)&ptr`, `union` из указателя и `u32`, `memcpy` указателя в 4 байта, указатель как ключ `u32` в контейнерах.
- После каждого проекта Win32 тоже собирается.

Приёмка: `Mixed|x64` и `Release|x64` собираются полностью; 0 предупреждений C4311, C4312, C4302; `dumpbin /dependents` всех x64-бинарников показывает только системные x64 DLL, DLL движка, `lua51.dll`, `OpenAL32.dll`, `d3dx9_43.dll`; Win32 проходит `smoke.ps1`.

### Этап 6. Первый запуск x64 и исправления — M

Порядок проверки: главное меню на R1 → R2 → новая игра → сохранение в x64 → загрузка этого сейва → переход уровня → видео → звук. Сейвы от Win32 в x64 не проверяются (совместимость не требуется).

Инструменты — раздел 5: резерв нижних 4 ГБ в `Mixed|x64`, точечный PageHeap.

Приёмка:

- чек-лист этапа 0 проходит на `Mixed|x64` с резервом нижних 4 ГБ и на `Release|x64`;
- сделан x64-эталон: сейв `smoke_reference_x64.sav` и логи `baseline-x64-r1.log`, `baseline-x64-r2.log`;
- `smoke.ps1 -Platform x64 -Save smoke_reference_x64` проходит на R1 и R2.

### Этап 7. Паритет и выпуск — M (в основном игровое тестирование)

- Отрезки сюжета на `Release|x64`: Кордон, Свалка, Агропром, Бар, Янтарь, Радар, Припять.
- Медиана FPS трёх прогонов `smoke.ps1` на свободной машине не хуже Win32 более чем на 10 %.
- Опционально: на тяжёлых настройках процесс превышает 4 ГБ без out-of-memory.
- Обновить `AGENTS.md` (сборка и деплой x64) и `README.md`.
- Тег `x64-1.0`. Win32-сборку оставить, пока она ничего не стоит; удалить при первой x64-only функции.

## 5. Как ловить 64-битные ошибки

1. Компилятор: `/we4311 /we4312 /we4302` для x64.
2. Резерв нижних 4 ГБ адресного пространства. В `Mixed|x64` в самом начале процесса, до инициализации xrCore, зарезервировать через `VirtualAlloc(MEM_RESERVE)` все свободные регионы ниже 4 ГБ. После этого новые выделения памяти получают адреса выше 4 ГБ, и усечение указателя падает сразу в месте ошибки, а не через час игры. Образы x64 EXE и DLL и так загружаются выше 4 ГБ (`/HIGHENTROPYVA`, база `0x140000000`). В `Release` резерв не включать.
3. PageHeap (`gflags /p /enable xrEngine.exe /full`) — только точечно, при подозрении на порчу кучи.
4. Lua: при старте писать в лог `jit.version` и статус JIT, чтобы по логу было видно, какая VM работает.

## 6. Раскладка рантайма

- Win32 без изменений: `binaries\`, `Launch.cmd`.
- x64: `binaries_x64\`, `Launch_x64.cmd`. Состав: `xrEngine.exe`, `xrCore.dll`, `xrCDB.dll`, `xrLua.dll`, `lua51.dll`, `xrSound.dll`, `OpenAL32.dll` (OpenAL Soft), `xrParticles.dll`, `xrNetServer.dll`, `xrCPU_Pipe.dll`, `xrRender_R1.dll`, `xrRender_R2.dll`, `xrGame.dll`, `ODE.dll`, `BugTrap.dll`. `d3dx9_43.dll` x64 — только если его нет в System32 (его ставит DirectX End-User Runtime). Нужен VC++ Redistributable x64 (CRT `/MD`).
- `appdata\` (сейвы, `user.ltx`) общий для обеих сборок. Сейвы Win32 в x64 не гарантируются, поэтому x64-сейвам давать отдельные имена.

## 7. Оценка

| Этап | Размер | Грубо, дней работы с агентами |
|---|---|---|
| 0. Базовая линия | S | 0,5–1 |
| 1. Удаление MP | L | 4–8 |
| 2. Переносимый код | M | 2–4 |
| 3. Зависимости | L | 4–8 |
| 4. Инфраструктура x64 | S | 1 |
| 5. Сборка x64 | M–L | 3–7 |
| 6. Первый запуск x64 | M | 3–6 |
| 7. Паритет | M | 1–2 недели тестов |

Итого грубо 4–7 недель. Самые рискованные места: 3.1 (Lua) и 1.3/1.5 (MP-классы в скриптах).

## 8. Промпты для агентов

Общая часть для каждого промпта:

> Работай по `Docs/x64-port-plan.md`, этап N, срез M. Выполни только работы этого среза, проверь приёмку и остановись. Соблюдай `AGENTS.md`: ноль комментариев в коде, данные игры только через оверрайды в `gamedata\`, Win32 после задачи собирается и запускается. В отчёте: что изменено, результат сборки, результат `smoke.ps1`, что не сделано и почему.

Срезы (этап 0 и срезы 1.0–1.3, 1.5 выполнены):

- **1.1–1.4:** удали <содержимое среза>. Не трогай `CSE_*`, `xrServer`, `xrClient`, `NET_Packet`. Проверки типа игры удаляй только там, где они ссылаются на удалённый код.
- **2:** замени inline asm и x86-only API на интринсики и C++ по списку этапа 2. Поведение Win32 не меняй. Сравни FPS с эталоном.
- **3.1:** замени LuaJIT 1.0.3 на LuaJIT 2.1 (`lua51.dll`, GC64 для x64), убери Coco, добавь алиасы только для реально используемых функций Lua 5.0. Прогони Lua-чек-лист.
- **3.2 / 3.3:** OpenAL Soft вместо router и EAX; раздели DX-библиотеки по платформам.
- **4:** добавь `Mixed|x64` и `Release|x64`, x64-пути вывода, `/we4311 /we4312 /we4302`, x64-сборку внешних библиотек, `binaries_x64` и `Launch_x64.cmd`.
- **5:** собери `<проект>` под x64 по правилам этапа 5. Массово не правь C4267/C4244.
- **6:** запусти x64 по порядку этапа 6, включи резерв нижних 4 ГБ в `Mixed|x64`, исправь найденное, сделай x64-эталон для `smoke.ps1`.

## 9. Решения

- Совместимость сейвов Win32 ↔ x64 не требуется (решение пользователя, 2026-09-28).
- Совместимость со сторонними модами не требуется (решение пользователя, 2026-09-28).
- Win32 поддерживается до конца этапа 7.
- Lua — LuaJIT 2.1. Альтернатива — стандартный Lua 5.1.5: проще в сборке, но скрипты работают медленнее, а проблема отказа от Coco остаётся той же.
