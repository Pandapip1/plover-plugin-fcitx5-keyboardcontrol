"""fcitx5 keyboard control backend registration.

Actually implemented in the compiled `plover_fcitx5._native` extension
(src/plugin/bridge.cpp+native.cpp at the top of this source tree, built
alongside the fcitx5 addon itself, src/bridge/ploverengine.cpp+h -- see
src/CMakeLists.txt), which is only imported once it's confirmed loadable.
This keeps importing *this* module -- e.g. to register the
`KeyboardCapture`/`KeyboardEmulation` plugins -- safe even when the
extension isn't (e.g. built against a different fcitx5/Python than what's
actually running); `get_missing_requirements` reports that instead, along
with whether the plover-fcitx5 addon is actually reachable.
"""

from plover.machine.keyboard_capture import Capture
from plover.output import Output

try:
    from plover_fcitx5 import _native
except ImportError as _import_error:
    _missing_requirement = str(_import_error)
else:
    _missing_requirement = None

__all__ = ["KeyboardCapture", "KeyboardEmulation"]


class _NativeRequirements:
    @classmethod
    def get_missing_requirements(cls) -> list[str]:
        if _missing_requirement is not None:
            return [
                "the compiled plover_fcitx5._native extension failed to "
                f"load: {_missing_requirement}"
            ]
        return _native.probe_addon()


if _missing_requirement is None:

    class KeyboardCapture(_NativeRequirements, Capture):
        def __init__(self):
            super().__init__()
            self._native = _native.Capture()

        def start(self):
            self._native.start(self._on_key_event)

        def cancel(self):
            self._native.cancel()

        def suppress(self, suppressed_keys=()):
            self._native.suppress(list(suppressed_keys))

        def _on_key_event(self, key, pressed):
            if pressed:
                self.key_down(key)
            else:
                self.key_up(key)

    class KeyboardEmulation(_NativeRequirements, Output):
        def __init__(self):
            super().__init__()
            self._native = _native.Emulation()

        def send_string(self, string):
            self._native.send_string(string)

        def send_backspaces(self, count):
            self._native.send_backspaces(count)

        def send_key_combination(self, combo):
            self._native.send_key_combination(combo)

        def set_key_press_delay(self, delay_ms):
            self._native.set_key_press_delay(delay_ms)

else:

    class KeyboardCapture(_NativeRequirements, Capture):
        pass

    class KeyboardEmulation(_NativeRequirements, Output):
        pass
