# ImGui 3D Overlay / ESP Visualization Tool

## Overview

This project is a lightweight **DirectX11 rendering framework** built around Dear ImGui.
It is designed for fast development of **real-time 3D overlays, debugging tools, and visualization systems**, including ESP-style rendering and scene inspection.

---

## Key Features

| Category          | Features                                                               |
| ----------------- | ---------------------------------------------------------------------- |
| Rendering         | DirectX11 pipeline, real-time drawing, primitives (boxes) |
| 3D System         | World-to-screen projection, camera system, transforms                  |
| ESP Visualization | 3D bounding boxes                                                      |
| UI System         | ImGui-based control panel, live toggles, runtime parameter editing     |

---

## Architecture

| Layer          | Description                                         |
| -------------- | --------------------------------------------------- |
| Rendering Core | DirectX11 device, swapchain, draw calls             |
| Scene System   | Entity model, transforms, camera projection logic   |
| UI Layer       | Dear ImGui interface, feature controls, debug menus |

---

## Debug Interface

| Tool              | Purpose                                               |
| ----------------- | ----------------------------------------------------- |
| Feature Toggles   | Enable / disable rendering features in real time      |
| Parameter Sliders | Adjust values live (FOV, distance, sensitivity, etc.) |
| Scene Viewer      | Inspect rendered objects                              |
| Performance HUD   | FPS + frame timing                                    |


---

## Requirements

| Component    | Requirement                |
| ------------ | -------------------------- |
| OS           | Windows 10 / 11            |
| Graphics API | DirectX 11                 |
| Compiler     | MSVC (Visual Studio 2022+) |
| Language     | C++17 or higher            |

---

## Dependencies

| Library     | Purpose                          |
| ----------- | -------------------------------- |
| Dear ImGui  | UI system                        |
| DirectXMath | Math / vectors / matrices        |
| Win32 API   | Window creation + input handling |

---

## Use Cases

| Area      | Usage                                      |
| --------- | ------------------------------------------ |
| Game Dev  | Debug overlays, entity visualization       |
| Rendering | Pipeline testing, scene inspection         |
| Tools     | Internal development tools, UI prototyping |
| Research  | Real-time 3D visualization experiments     |

## Preview
<img width="1920" height="1080" alt="4" src="https://github.com/user-attachments/assets/ee0f2cb7-9336-4519-bb94-a049878f5443" />
<img width="1920" height="1080" alt="3" src="https://github.com/user-attachments/assets/9927fe74-daac-4fda-ac24-705620e34420" />

