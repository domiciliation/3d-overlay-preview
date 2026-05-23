# ImGui 3D ESP Preview Renderer

## Overview

This project is a lightweight **DirectX11 + Dear ImGui rendering tool** designed to simulate how an **in-game ESP system would look in real conditions**.

Instead of relying on live game rendering, it generates a **fictional 3D representation of a character model** inside an ImGui viewport.
This allows developers to preview and fine-tune ESP visuals in a controlled environment.

The current implementation supports **3D bounding box rendering**, with a structure designed to be extended to skeletons and other ESP elements.

---

## Purpose

The main goal of this tool is to provide a **visual sandbox for ESP development**, where you can:

* Preview ESP rendering without being in-game
* Test box scaling, padding, and positioning
* Validate visual appearance of overlays
* Simulate how ESP will behave in real scenarios

It acts as a **debug / development environment for ESP systems**, not as an in-game cheat itself.

---

## Key Features

* Real-time **3D model rendering (OBJ-based)**
* ImGui-integrated viewport system
* Simulated camera (rotation + zoom)
* World-space to screen-space projection
* Adjustable **3D bounding boxes (AABB hitbox system)**
* Wireframe visualization overlay
* Offscreen rendering into ImGui window
* Live parameter tuning for ESP adjustments

---

## Architecture Overview

### Rendering System

Handles DirectX11 initialization, shader pipeline, and offscreen rendering to a texture displayed inside ImGui.

### 3D Simulation Layer

Loads a model (used as a placeholder for a player character) and normalizes it into a centered unit space to simulate consistent in-game scale.

### ESP Visualization Layer

Currently implements:

* 3D bounding box (AABB-based)

Designed to be extended with:

* Skeleton ESP
* Bone visualization
* Health bars
* Name tags
* Distance indicators

### UI Layer (ImGui)

Provides real-time controls for:

* Hitbox activation
* Box padding adjustments (X / Y / Z)
* Camera rotation
* Zoom control
* Live preview interaction

---

## Current ESP Features

| Feature        | Status      | Description                          |
| -------------- | ----------- | ------------------------------------ |
| 3D Box ESP     | Implemented | AABB-based bounding box around model |
| Skeleton ESP   | Planned     | Bone-based visualization system      |
| Player Model   | Simulated   | OBJ model used as placeholder        |
| Camera Control | Implemented | Orbit + zoom system                  |
| UI Controls    | Implemented | Real-time ImGui parameter editing    |

---

## Requirements

| Component    | Requirement                |
| ------------ | -------------------------- |
| OS           | Windows 10 / 11            |
| Graphics API | DirectX 11                 |
| Compiler     | MSVC (Visual Studio 2022+) |
| Language     | C++17 or higher            |

---

## How It Works

1. A 3D model is loaded from an OBJ file (used as a fake player)
2. The model is normalized into a centered unit space
3. A DirectX11 scene renders the model in real time
4. A bounding box (ESP box) is computed from AABB data
5. The scene is rendered into a texture
6. That texture is displayed inside an ImGui viewport
7. The user can rotate, zoom, and adjust ESP parameters live

---

## Intended Use

This tool is meant for:

* ESP development testing
* Visual debugging of overlay systems
* Prototyping game cheat visuals (boxes, skeletons, etc.)
* Rendering experimentation in DirectX11 + ImGui

---

## Preview

<img width="1920" height="1080" alt="4" src="https://github.com/user-attachments/assets/ee0f2cb7-9336-4519-bb94-a049878f5443" />
<img width="1920" height="1080" alt="3" src="https://github.com/user-attachments/assets/9927fe74-daac-4fda-ac24-705620e34420" />

---

## Notes

* Only box ESP is currently implemented
* Skeleton system is prepared but not yet added
* Fully modular rendering pipeline (easy to extend)
* Designed for experimentation and ESP UI development
