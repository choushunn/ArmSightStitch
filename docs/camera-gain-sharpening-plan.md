# Plan: Add Gain + Sharpening Controls to Camera UI

## Context

The ToupCam SDK provides two image-quality controls that are currently unused in the UI:

**1. Analog Gain (ExpoAGain)** — SDK functions `Toupcam_put_ExpoAGain` / `Toupcam_get_ExpoAGain` / `Toupcam_get_ExpoAGainRange`. `CameraHandler` already has `setGain()`/`getGain()` but no `getGainRange()` and no UI widget.

**2. Sharpening** — SDK option `TOUPCAM_OPTION_SHARPENING` (0x1e), a packed 32-bit value: `(threshold << 24) | (radius << 16) | strength`. Not used anywhere in the codebase — no interface method, no implementation, no UI.

This plan adds both controls to the UI following the established exposure-control pattern.

---

## Part A: Gain Control

### A1. `core/camera/ICameraHandler.h` — Add `getGainRange()` pure virtual

After line 63 (`virtual float getGain() const = 0;`):
```cpp
virtual void getGainRange(unsigned short& min_pct, unsigned short& max_pct, unsigned short& def_pct) const = 0;
```

### A2. `core/camera/CameraHandler.h` — Declare `getGainRange()`

After line 49 (`float getGain() const;`):
```cpp
void getGainRange(unsigned short& min_pct, unsigned short& max_pct, unsigned short& def_pct) const;
```

### A3. `core/camera/CameraHandler.cpp` — Implement `getGainRange()`

After line 273:
```cpp
void CameraHandler::getGainRange(unsigned short& min_pct, unsigned short& max_pct, unsigned short& def_pct) const {
    if (!connected_) { min_pct = 100; max_pct = 500; def_pct = 100; return; }
    if (FAILED(Toupcam_get_ExpoAGainRange(hcam_, &min_pct, &max_pct, &def_pct))) {
        min_pct = 100; max_pct = 500; def_pct = 100;
    }
}
```

### A4. `ui/MainWindow.ui` — Add `cameraGainRow` after exposure row (after line 156)

```xml
              <item>
               <layout class="QHBoxLayout" name="cameraGainRow">
                <property name="spacing"><number>4</number></property>
                <item>
                 <widget class="QLabel" name="gainLabel">
                  <property name="text"><string>增益</string></property>
                 </widget>
                </item>
                <item>
                 <widget class="QSlider" name="gainSlider">
                  <property name="enabled"><bool>false</bool></property>
                  <property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property>
                  <property name="minimum"><number>100</number></property>
                  <property name="maximum"><number>500</number></property>
                  <property name="value"><number>100</number></property>
                  <property name="singleStep"><number>10</number></property>
                  <property name="pageStep"><number>50</number></property>
                 </widget>
                </item>
                <item>
                 <widget class="QLabel" name="gainValueLabel">
                  <property name="minimumSize"><size><width>60</width><height>0</height></size></property>
                  <property name="text"><string>1.00x</string></property>
                  <property name="alignment"><enum>Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignVCenter</enum></property>
                 </widget>
                </item>
               </layout>
              </item>
```

### A5. `ui/MainWindow.cpp` — Gain wiring (6 sites)

#### A5a. Constructor — initial disabled (after line 68)
```cpp
ui_->gainSlider->setEnabled(false);
ui_->gainValueLabel->setEnabled(false);
```

#### A5b. connectSignals — gain slider (after line 672)
```cpp
    // ---- 相机增益控制 ----
    connect(ui_->gainSlider, &QSlider::valueChanged, this, [this](int val) {
        float gain = static_cast<float>(val) / 100.0f;
        ui_->gainValueLabel->setText(QString::number(gain, 'f', 2) + "x");
        if (!ctrl_.cameraHandler().getAutoExposure()) {
            ctrl_.cameraHandler().setGain(gain);
        }
    });
```

#### A5c. connectSignals — extend autoExposureCheck handler
Inside the existing lambda (line 650), after line 654 add:
```cpp
        ui_->gainSlider->setEnabled(!auto_on);
        ui_->gainValueLabel->setEnabled(!auto_on);
```
After line 662 (exposure label set), add gain sync:
```cpp
        float current_gain = ctrl_.cameraHandler().getGain();
        int gain_val = qBound(ui_->gainSlider->minimum(),
                              static_cast<int>(current_gain * 100.0f),
                              ui_->gainSlider->maximum());
        ui_->gainSlider->setValue(gain_val);
        ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + "x");
```

#### A5d. connectSignals — extend cameraExposureChanged handler (after line 845)
Add gain sync with QSignalBlocker:
```cpp
    float current_gain = ctrl_.cameraHandler().getGain();
    {
        QSignalBlocker gain_blocker(ui_->gainSlider);
        int gval = qBound(ui_->gainSlider->minimum(),
                          static_cast<int>(current_gain * 100.0f),
                          ui_->gainSlider->maximum());
        ui_->gainSlider->setValue(gval);
    }
    ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + "x");
```

#### A5e. on_connectCamera — sync gain from hardware (after line 983)
```cpp
    // Sync gain UI state from camera
    {
        unsigned short gMin, gMax, gDef;
        ctrl_.cameraHandler().getGainRange(gMin, gMax, gDef);
        auto& gslider = ui_->gainSlider;
        gslider->setMinimum(static_cast<int>(gMin));
        gslider->setMaximum(static_cast<int>(gMax));
        float current_gain = ctrl_.cameraHandler().getGain();
        int gval = qBound(gslider->minimum(), static_cast<int>(current_gain * 100.0f), gslider->maximum());
        gslider->setValue(gval);
        ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + "x");
        gslider->setEnabled(!auto_on);
        ui_->gainValueLabel->setEnabled(!auto_on);
    }
```

#### A5f. on_disconnectCamera — disable gain widgets (after line 1003)
```cpp
    ui_->gainSlider->setEnabled(false);
    ui_->gainValueLabel->setEnabled(false);
```

---

## Part B: Sharpening Control

### API Summary

The ToupCam SDK sharpening is controlled via `Toupcam_put_Option(h, TOUPCAM_OPTION_SHARPENING, packed)` / `Toupcam_get_Option(h, TOUPCAM_OPTION_SHARPENING, &packed)`. The value is packed:
```
(threshold << 24) | (radius << 16) | strength
```
- **strength**: [0, 500], default 0 (0 = disabled)
- **radius**: [1, 10], default 2
- **threshold**: [0, 255], default 0

### B1. `core/camera/ICameraHandler.h` — Add sharpening methods

After `getGainRange()` (newly added in A1):
```cpp
virtual bool setSharpening(unsigned short strength) = 0;
virtual unsigned short getSharpening() const = 0;
```

### B2. `core/camera/CameraHandler.h` — Declare sharpening methods

After `getGainRange()` (newly added in A2):
```cpp
bool setSharpening(unsigned short strength);
unsigned short getSharpening() const;
```

### B3. `core/camera/CameraHandler.cpp` — Implement sharpening

After `getGainRange()` implementation (newly added in A3):
```cpp
bool CameraHandler::setSharpening(unsigned short strength) {
    if (!connected_) return false;
    // Pack: (threshold << 24) | (radius << 16) | strength
    // Use defaults: radius=2, threshold=0
    int value = (0 << 24) | (2 << 16) | strength;
    return SUCCEEDED(Toupcam_put_Option(hcam_, TOUPCAM_OPTION_SHARPENING, value));
}

unsigned short CameraHandler::getSharpening() const {
    if (!connected_) return 0;
    int value = 0;
    if (FAILED(Toupcam_get_Option(hcam_, TOUPCAM_OPTION_SHARPENING, &value))) return 0;
    return static_cast<unsigned short>(value & 0xFFFF);  // extract strength from low 16 bits
}
```

### B4. `ui/MainWindow.ui` — Add `cameraSharpeningRow` after gain row

Insert after the new gain row block:
```xml
              <item>
               <layout class="QHBoxLayout" name="cameraSharpeningRow">
                <property name="spacing"><number>4</number></property>
                <item>
                 <widget class="QLabel" name="sharpeningLabel">
                  <property name="text"><string>锐化</string></property>
                 </widget>
                </item>
                <item>
                 <widget class="QSlider" name="sharpeningSlider">
                  <property name="enabled"><bool>false</bool></property>
                  <property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property>
                  <property name="minimum"><number>0</number></property>
                  <property name="maximum"><number>500</number></property>
                  <property name="value"><number>0</number></property>
                  <property name="singleStep"><number>10</number></property>
                  <property name="pageStep"><number>50</number></property>
                 </widget>
                </item>
                <item>
                 <widget class="QLabel" name="sharpeningValueLabel">
                  <property name="minimumSize"><size><width>50</width><height>0</height></size></property>
                  <property name="text"><string>关</string></property>
                  <property name="alignment"><enum>Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignVCenter</enum></property>
                 </widget>
                </item>
               </layout>
              </item>
```

### B5. `ui/MainWindow.cpp` — Sharpening wiring

#### B5a. Constructor — initial disabled (after A5a)
```cpp
ui_->sharpeningSlider->setEnabled(false);
ui_->sharpeningValueLabel->setEnabled(false);
```

#### B5b. connectSignals — sharpening slider (after A5b)
```cpp
    // ---- 相机锐化控制 ----
    connect(ui_->sharpeningSlider, &QSlider::valueChanged, this, [this](int val) {
        if (val == 0)
            ui_->sharpeningValueLabel->setText(QStringLiteral("关"));
        else
            ui_->sharpeningValueLabel->setText(QString::number(val));
        ctrl_.cameraHandler().setSharpening(static_cast<unsigned short>(val));
    });
```

#### B5c. on_connectCamera — sync sharpening (after A5e)
```cpp
    // Sync sharpening UI state from camera
    {
        unsigned short current_sharp = ctrl_.cameraHandler().getSharpening();
        QSignalBlocker sb(ui_->sharpeningSlider);
        ui_->sharpeningSlider->setMinimum(0);
        ui_->sharpeningSlider->setMaximum(500);
        ui_->sharpeningSlider->setValue(static_cast<int>(current_sharp));
        ui_->sharpeningValueLabel->setText(current_sharp == 0 ? QStringLiteral("关")
                                            : QString::number(current_sharp));
        ui_->sharpeningSlider->setEnabled(true);
        ui_->sharpeningValueLabel->setEnabled(true);
    }
```

#### B5d. on_disconnectCamera — disable sharpening (after A5f)
```cpp
    ui_->sharpeningSlider->setEnabled(false);
    ui_->sharpeningValueLabel->setEnabled(false);
```

---

## Design Decisions Summary

| Aspect | Gain | Sharpening |
|--------|------|------------|
| Auto-exposure interaction | Slider disabled when AE ON (AE controls gain) | Independent — always adjustable |
| Range source | Hardware query (`Toupcam_get_ExpoAGainRange`) | Fixed macros (0–500) |
| Slider scale | value = gain × 100 (display: "1.50x") | value = strength (display: "150" or "关" at 0) |
| Hardware sync on AE event | Yes (via `cameraExposureChanged`) | N/A (sharpening is static, no SDK events) |

## Verification

1. Build: `cd build/vcpkg-mingw && ninja`
2. Run `bin/ArmSightStitch.exe`
3. **Gain**: Connect camera → uncheck "自动曝光" → move gain slider → verify brightness changes + label shows "1.50x" etc. → check AE → slider disables, label tracks hardware → disconnect → both disable
4. **Sharpening**: Connect camera → move sharpening slider from 0 → verify "关" changes to number → verify image sharpens (visible in preview) → set back to 0 → shows "关" → disconnect → slider/label disable
