/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

class Vector2 {
public:
    Vector2(float X, float Y) { m_Values[0] = X; m_Values[1] = Y; }
    ~Vector2() { }

    float GetX() const { return m_Values[0]; }
    float GetY() const { return m_Values[1]; }
    const float *GetPointer() const { return (const float*)&m_Values[0]; }
private:
    float m_Values[2];
};

class Vector3 {
public:
    Vector3(float X, float Y, float Z) { m_Values[0] = X; m_Values[1] = Y; m_Values[2] = Z; }
    ~Vector3() { }

    float GetX() const { return m_Values[0]; }
    float GetY() const { return m_Values[1]; }
    float GetZ() const { return m_Values[2]; }
    const float *GetPointer() const { return (const float*)&m_Values[0]; }
private:
    float m_Values[3];
};

class Vector4 {
public:
    Vector4(float X, float Y, float Z, float W) { m_Values[0] = X; m_Values[1] = Y; m_Values[2] = Z; m_Values[3] = W; }
    ~Vector4() { }

    float GetX() const { return m_Values[0]; }
    float GetY() const { return m_Values[1]; }
    float GetZ() const { return m_Values[2]; }
    float GetW() const { return m_Values[3]; }
    const float *GetPointer() const { return (const float*)&m_Values[0]; }
private:
    float m_Values[4];
};
