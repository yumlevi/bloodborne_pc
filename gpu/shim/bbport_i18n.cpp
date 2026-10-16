// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the in-game menu in Russian (bbport.ini menu_language=ru). The menu's strings are
// English; Tr() maps each to its Russian text. A string missing here stays English.
#include "bbport_i18n.h"

#include <string_view>
#include <unordered_map>
#include "bbport_settings.h"

namespace BbI18n {

namespace {

struct Pair {
    const char* english;
    const char* russian;
};

constexpr Pair kRussian[] = {
    {"Bloodborne — settings  (Insert / L3+R3)",
     "Bloodborne — настройки  (Insert / L3+R3)"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f мс)"},
    {"Temporal upscaler",
     "Временной апскейлер"},
    {"Off",
     "Выкл"},
    {"TAA (native anti-aliasing)",
     "TAA (нативное сглаживание)"},
    {"Upscaler",
     "Апскейлер"},
    {"— not supported by this GPU",
     "— не поддерживается видеокартой"},
    {"— in progress",
     "— в работе"},
    {"Upscaler unavailable: %s",
     "Апскейлер недоступен: %s"},
    {"The mode selected above is active. You can select it again to retry.",
     "Активен режим, выбранный выше. Его можно выбрать снова, чтобы повторить попытку."},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, replayed on Vulkan (output "
     "matches the DLL). One model for Native..Performance and a separate one for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (needs the DLLs and Proton).",
     "FSR 4.1.1 в режиме INT8: модель из DLL AMD 4.1.1, воспроизведённая в Vulkan "
     "(результат совпадает с DLL). Одна модель для Native..Performance и отдельная для "
     "Ultra Performance. Ассеты: tools/fsr4cap/build_assets.sh (нужны DLL и Proton)."},
    {"FSR 4 in INT8 mode (model v07 from AMD FidelityFX SDK sources). Better quality than "
     "FSR 3.1, but a heavier pass. Changing the preset rebuilds the model (a short pause). "
     "Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 в режиме INT8 (модель v07 из исходников AMD FidelityFX SDK). Качество выше, "
     "чем у FSR 3.1, но проход тяжелее. Смена пресета пересобирает модель (короткая "
     "пауза). Ассеты: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: авто-экспозиция"},
    {"FSR 4: invert jitter sign",
     "FSR 4: обратный знак jitter"},
    {"For ghosting checks: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to drop past frames. Applied immediately, no restart.",
     "Проверка при гостинге: сеть FSR 4 нормирует цвет по экспозиции и по ней решает, "
     "когда отбросить прошлые кадры. Меняются сразу, без перезапуска."},
    {"Preset",
     "Пресет"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, рендер %dx%d)"},
    {"TAA anti-aliases the scene at the output resolution, without an FSR model or "
     "upscaling. The saved preset returns when an upscaler is selected.",
     "TAA сглаживает сцену в разрешении вывода, без модели FSR и апскейлинга. Сохранённый "
     "пресет FSR восстановится при выборе FSR."},
    {"Active scene render: %d x %d",
     "Активный рендер сцены: %d x %d"},
    {"Preset at startup: %s",
     "Пресет при запуске: %s"},
    {"Output resolution",
     "Разрешение вывода"},
    {"Size of the final frame and the UI. The preset sets the scene size relative to the "
     "output: 4K Performance = 1920x1080. Applies after restarting the game.",
     "Размер готового кадра и интерфейса. Пресет задаёт размер сцены относительно вывода: "
     "4K Performance = 1920x1080. Применяется после перезапуска игры."},
    {"The size of the final frame and the UI changes at the next frame boundary. The "
     "preset sets the scene size relative to the output: 4K Performance = 1920x1080. "
     "Changing the size resets the upscaler history and may cause a short pause.",
     "Размер готового кадра и интерфейса меняется на границе следующего кадра. Пресет "
     "задаёт размер сцены относительно вывода: 4K Performance = 1920x1080. Смена размера "
     "сбрасывает историю FSR и может вызвать короткую паузу."},
    {"Auto (by GPU)",
     "Авто (по видеокарте)"},
    {"Off (faster)",
     "Выключена (быстрее)"},
    {"On",
     "Включена"},
    {"Live resolution changes",
     "Смена разрешения на лету"},
    {"On: output resolution and preset change without a restart, but the game's "
     "post-processing stays at 1080p — noticeably slower on the Steam Deck and older GPUs. "
     "Off: everything renders at the preset resolution; changes need a restart. Auto turns "
     "it on for powerful discrete GPUs. Applies after restarting the game.",
     "Включена: разрешение вывода и пресет меняются без перезапуска, но постобработка игры "
     "остаётся в 1080p — на Steam Deck и старых видеокартах это заметно медленнее. "
     "Выключена: всё рисуется в разрешении пресета, смена — через перезапуск. Авто "
     "включает её на мощных дискретных видеокартах. Применяется после перезапуска игры."},
    {"Game effects (after restart)",
     "Эффекты игры (после перезапуска)"},
    {"Highest (-2)",
     "Максимальная (-2)"},
    {"As in the game",
     "Как в игре"},
    {"Lower (1)",
     "Ниже (1)"},
    {"Lowest (2)",
     "Минимальная (2)"},
    {"Model detail",
     "Детализация моделей"},
    {"Effects are switched on and off by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights cost noticeable GPU time.",
     "Эффекты включаются и выключаются патчами игры при запуске (patches/Bloodborne.xml). "
     "Размытие в движении и тени от динамических источников заметно нагружают GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left "
     "touchpad / Tab. Needs DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from "
     "Nexus mod #253. Right touchpad: Backspace.",
     "Свободная камера: удерживайте Cross и нажимайте L3 (клавиатура: Space + Z). Debug "
     "menu: левый touchpad / Tab. Нужны DbgFont14h.ccm и DbgFont14h.tpf в dvdroot_ps4/font "
     "из мода Nexus #253. Правый touchpad: Backspace."},
    {"Changes apply after restarting the game",
     "Изменения применятся после перезапуска игры"},
    {"Apply and restart the game",
     "Применить и перезапустить игру"},
    {"Keyboard and mouse",
     "Клавиатура и мышь"},
    {"Mouse camera (F1 releases the mouse)",
     "Камера мышью (F1 освобождает мышь)"},
    {"The captured mouse turns the camera directly; the right stick or the camera keys hand "
     "it back to the game. Bindings, several per control (key_r3=c,mouse_middle,left_alt), "
     "are the key_* lines of bbport.ini.",
     "Захваченная мышь поворачивает камеру напрямую; правый стик или клавиши камеры "
     "возвращают её игре. Назначения, по несколько на кнопку (key_r3=c,mouse_middle,left_alt), "
     "задаются строками key_* в bbport.ini."},
    {"Mouse sensitivity",
     "Чувствительность мыши"},
    {"Invert vertical mouse",
     "Инвертировать мышь по вертикали"},
    {"Walk (stick push with key_walk, Left Ctrl)",
     "Шаг (наклон стика с key_walk, левый Ctrl)"},
    {"Borderless fullscreen (F11)",
     "Полноэкранное окно без рамки (F11)"},
    {"Other",
     "Прочее"},
    {"FPS counter in the corner",
     "Счётчик FPS в углу"},
    {"Close",
     "Закрыть"},
    {"Settings are saved to bbport.ini",
     "Настройки сохраняются в bbport.ini"},
    {"%.0f FPS  %.1f ms  %s",
     "%.0f FPS  %.1f мс  %s"},
    {"Chromatic aberration",
     "Хроматическая аберрация"},
    {"Depth of field (DoF)",
     "Глубина резкости (DoF)"},
    {"Motion blur",
     "Размытие в движении"},
    {"SSAO ambient occlusion",
     "Затенение SSAO"},
    {"The game's own anti-aliasing",
     "Собственное сглаживание игры"},
    {"Shadows from dynamic lights",
     "Тени от динамических источников"},
    {"SSR reflections (not in the original game)",
     "Отражения SSR (не было в игре)"},
    {"Skip intro videos at startup",
     "Пропуск заставок при запуске"},
    {"Free camera (Cross + L3)",
     "Свободная камера (Cross + L3)"},
    {"Debug menu (needs font files)",
     "Debug menu (нужны файлы шрифтов)"},
    {"With an output other than 1080p the whole game renders at the preset resolution "
     "(patched at startup): fastest on the Steam Deck and weaker GPUs. Preset or output "
     "changes apply after a restart. \"Live resolution changes\" below allows changing "
     "without a restart (post-processing then stays at 1080p, slower).",
     "При выводе не 1080p вся игра рисуется в разрешении пресета (патч при запуске): это "
     "быстрее всего на Steam Deck и слабых GPU. Смена пресета или разрешения вывода — "
     "после перезапуска. Пункт «Смена разрешения на лету» ниже включает смену без "
     "перезапуска (постобработка тогда остаётся в 1080p, медленнее)."},
    {"BB_RENDER_RES fixes the scene size at startup. Remove this explicit variable to "
     "change resolution and presets without restarting the game.",
     "BB_RENDER_RES фиксирует размер сцены при запуске. Уберите эту явную переменную для "
     "смены разрешения и пресетов без перезапуска игры."},
    {"Native AA: the upscaler works as anti-aliasing. The other presets lower the scene "
     "render resolution relative to the output. The UI is drawn at the output resolution. "
     "The preset applies from the next frame without restarting the game.",
     "Native AA: апскейлер работает как сглаживание. Остальные пресеты уменьшают "
     "разрешение отрисовки сцены относительно вывода. Интерфейс рисуется в разрешении "
     "вывода. Пресет применяется со следующего кадра без перезапуска игры."},
    {"Sharpening (RCAS)",
     "Резкость (RCAS)"},
    {"Sharpness",
     "Сила резкости"},
    {"Up to 1: the upscaler's own sharpening (RCAS). Above 1 another RCAS pass is added. "
     "DLSS has no sharpening of its own: an RCAS pass does all of it. Ctrl+click the "
     "slider to type an exact value.",
     "До 1 — резкость самого апскейлера (RCAS). Выше 1 добавляется ещё один проход RCAS. У "
     "DLSS своей резкости нет: её целиком даёт проход RCAS. Ctrl+клик по ползунку — ввести "
     "точное значение."},
    {"Sub-pixel jitter",
     "Субпиксельный сдвиг (jitter)"},
    {"Each frame the scene shifts by a fraction of a pixel, and the upscaler gathers more "
     "detail from several frames. Without it you only get history-based anti-aliasing.",
     "Каждый кадр сцена сдвигается на долю пикселя, и апскейлер собирает из нескольких "
     "кадров больше деталей. Без него получается только сглаживание по истории."},
    {"Reactive mask",
     "Маска реактивности"},
    {"Enable mask",
     "Включить маску"},
    {"Marks transparent effects (particles, haze) so the upscaler relies less on past "
     "frames. Fewer trails behind effects, but shimmer returns underneath them.",
     "Помечает прозрачные эффекты (частицы, дымку), чтобы апскейлер меньше опирался на "
     "прошлые кадры. Меньше шлейфов за эффектами, но под ними возвращается дрожание."},
    {"Scale",
     "Масштаб"},
    {"Threshold",
     "Порог"},
    {"Maximum",
     "Максимум"},
    {"Show mask (debug)",
     "Показать маску (отладка)"},
    {"Character motion vectors",
     "Векторы движения персонажей"},
    {"Exact vectors for animated objects: clothing and weapons break up less in motion. A "
     "static scene gets no extra pass. The change applies after restarting the game.",
     "Точные векторы для анимированных объектов: одежда и оружие меньше рассыпаются при "
     "движении. Статичная сцена не получает дополнительный проход. Изменение применяется "
     "после перезапуска игры."},
    {"Show motion vectors (debug)",
     "Показать векторы движения (отладка)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "got an exact object vector, not only camera motion. A moving object with neither "
     "blue nor red/green is treated as still by the upscaler, hence the trail.",
     "Красный/зелёный: движение по горизонтали/вертикали (8 пикселей = полная яркость). "
     "Синий: пиксель получил точный вектор объекта, а не только движение камеры. "
     "Движущийся предмет без синего и без красного/зелёного апскейлер считает неподвижным, "
     "отсюда шлейф."},
    {"Shown with 1920 x 1080 output only (the mask view is part of that path).",
     "Только при выводе 1920 x 1080 (просмотр маски — часть этого пути)."},
    {"Menu language",
     "Язык меню"},
    {"Enter: OK    Esc: cancel    The system input method types here too",
     "Enter: OK    Esc: отмена    Системный способ ввода тоже печатает здесь"},
    {"The language of this menu. The game's own language is set by the launcher "
     "(BB_LANGUAGE).",
     "Язык этого меню. Язык самой игры задаёт лаунчер (BB_LANGUAGE)."},
};

} // namespace

const char* Tr(const char* english) {
    if (BbSettings::Get().menu_language != BbSettings::MenuRussian || !english) {
        return english;
    }
    static const auto table = [] {
        std::unordered_map<std::string_view, const char*> map;
        for (const auto& pair : kRussian) {
            map.emplace(pair.english, pair.russian);
        }
        return map;
    }();
    const auto it = table.find(english);
    return it != table.end() ? it->second : english;
}

} // namespace BbI18n
