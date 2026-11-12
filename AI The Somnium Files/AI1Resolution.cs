using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using UnityEngine;

public static class AI1Resolution
{
    private static readonly Dictionary<Camera, Matrix4x4> Projections =
        new Dictionary<Camera, Matrix4x4>();
    private static readonly List<string> ReportedErrors = new List<string>();
    private static bool layoutLogged, projectionLogged;
    internal static bool PreserveHUDAspect = true;
    private const string HUDWrapperName = "AI1_HUDAspect";

    internal static Vector2 HUDScale(float stretchX, float stretchY)
    {
        if (!(stretchX > 0f) || !(stretchY > 0f)
            || float.IsInfinity(stretchX) || float.IsInfinity(stretchY))
        {
            ErrorOnce("HUD ancestor scale is zero or invalid; leaving its scale correction neutral.");
            return Vector2.one;
        }
        var uniform = Math.Min(stretchX, stretchY);
        return new Vector2(uniform / stretchX, uniform / stretchY);
    }

    private static void WrapHUD(RectTransform child, RectTransform parent, bool centered)
    {
        var index = child.GetSiblingIndex();
        var wrapper = new GameObject(HUDWrapperName, typeof(RectTransform))
            .GetComponent<RectTransform>();
        wrapper.SetParent(parent, false);
        wrapper.SetSiblingIndex(index);
        wrapper.anchorMin = Vector2.zero;
        wrapper.anchorMax = Vector2.one;
        wrapper.pivot = parent.pivot;
        wrapper.sizeDelta = Vector2.zero;
        child.SetParent(wrapper, false);
        UpdateHUDWrapper(wrapper, centered);
    }

    private static void UpdateHUDWrapper(RectTransform wrapper, bool centered)
    {
        var parent = wrapper.parent as RectTransform;
        var scale = wrapper.lossyScale;
        var local = wrapper.localScale;
        var inheritedX = Math.Abs(scale.x / local.x);
        var inheritedY = Math.Abs(scale.y / local.y);
        var correction = PreserveHUDAspect ? HUDScale(inheritedX, inheritedY) : Vector2.one;
        wrapper.localScale = new Vector3(correction.x, correction.y, 1f);
        var center = centered ? parent.rect.center : Vector2.zero;
        wrapper.anchoredPosition = new Vector2(
            center.x * (1f - correction.x), center.y * (1f - correction.y));
    }

    private static void CorrectHUDChildren(RectTransform parent, bool centered)
    {
        var children = new List<RectTransform>();
        for (int i = 0; i < parent.childCount; i++)
        {
            var child = parent.GetChild(i) as RectTransform;
            if (child != null)
                children.Add(child);
        }
        foreach (var child in children)
        {
            if (child.name == HUDWrapperName)
            {
                UpdateHUDWrapper(child, centered);
                continue;
            }
            if (child.name == "Windows")
            {
                CorrectHUDChildren(child, true);
                continue;
            }
            if (child.name == "UIOff1" && child.Find("CursorPointer") != null)
            {
                var pointer = child.Find("CursorPointer") as RectTransform;
                if (pointer.GetComponent<AI1CursorAspect>() == null)
                    pointer.gameObject.AddComponent<AI1CursorAspect>();
                continue;
            }
            if (child.name == "Command")
            {
                CorrectHUDChildren(child, false);
                continue;
            }
            if (child.name == "RightWindow" || child.name == "LeftWindow"
                || child.name == "LeftWindow2" || child.name == "Fade"
                || child.name == "FadePlane" || child.name == "FilterTop"
                || child.name == "FilterBlur" || child.name == "TutorialBlack"
                || child.name == "PlayEnd")
                continue;
            if (PreserveHUDAspect)
                WrapHUD(child, parent, centered);
        }
    }

    private static void ErrorOnce(string message)
    {
        if (!ReportedErrors.Contains(message))
        {
            ReportedErrors.Add(message);
            Debug.LogError("[AI1_ARFix] " + message);
        }
    }

    public static void AdjustLayout(RectTransform rect)
    {
        if (rect == null)
        {
            ErrorOnce("Gameplay layout has no RectTransform.");
            return;
        }
        var parent = rect.parent as RectTransform;
        if (parent == null || rect.name != "ScreenScaler")
        {
            ErrorOnce("Unexpected gameplay layout hierarchy; not scaling it.");
            return;
        }
        var bounds = parent.rect;
        if (!(bounds.width > 0f) || !(bounds.height > 0f)
            || float.IsInfinity(bounds.width) || float.IsInfinity(bounds.height))
        {
            ErrorOnce("Invalid gameplay canvas dimensions; not scaling it.");
            return;
        }
        var scale = rect.localScale;
        scale.x *= bounds.width / 1920f;
        scale.y *= bounds.height / 1080f;
        if (rect.gameObject.scene.name == "OptionMenu")
        {
            if (PreserveHUDAspect)
            {
                var correction = HUDScale(
                    Math.Abs(parent.lossyScale.x * scale.x),
                    Math.Abs(parent.lossyScale.y * scale.y));
                var center = rect.rect.center;
                var position = rect.anchoredPosition;
                rect.anchoredPosition = new Vector2(
                    position.x + center.x * scale.x * (1f - correction.x),
                    position.y + center.y * scale.y * (1f - correction.y));
                scale.x *= correction.x;
                scale.y *= correction.y;
            }
            rect.localScale = scale;
        }
        else
        {
            rect.localScale = scale;
            CorrectHUDChildren(rect, true);
        }
        if (!layoutLogged)
        {
            Debug.Log("[AI1_ARFix] Gameplay surface/cursor scale: " + scale.x + ", "
                + scale.y + "; canvas: " + bounds.width + "x" + bounds.height);
            layoutLogged = true;
        }
    }

    public static void ApplyProjection(Camera camera, int renderTextureType)
    {
        if (camera != null && Projections.ContainsKey(camera))
        {
            ErrorOnce("A gameplay projection was not restored before the next render.");
            RestoreProjection(camera);
        }
        SetProjection(camera, renderTextureType);
    }

    private static bool SetProjection(Camera camera, int renderTextureType)
    {
        if (camera == null || renderTextureType < 1 || renderTextureType > 4
            || camera.orthographic)
            return false;
        var aspect = camera.aspect;
        if (!(aspect > 0f) || float.IsInfinity(aspect))
        {
            ErrorOnce("Invalid gameplay camera aspect; not correcting projection.");
            return false;
        }
        var factor = Math.Min(1f, aspect / (16f / 9f));
        if (factor == 1f)
            return false;
        var original = camera.projectionMatrix;
        var corrected = original;
        corrected.m00 *= factor;
        corrected.m11 *= factor;
        Projections.Add(camera, original);
        camera.projectionMatrix = corrected;
        if (!projectionLogged)
        {
            Debug.Log("[AI1_ARFix] Narrow gameplay projection: aspect=" + aspect
                + ", factor=" + factor + ". Rendering and picking use the same correction.");
            projectionLogged = true;
        }
        return true;
    }

    private static bool BeginQuery(Camera camera, int renderTextureType)
    {
        return camera != null && !Projections.ContainsKey(camera)
            && SetProjection(camera, renderTextureType);
    }

    public static Ray ViewportPointToRay(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.ViewportPointToRay(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static bool ImagePointToRay(RectTransform rect, Camera uiCamera, Camera camera,
        int renderTextureType, Vector2 screenPoint, out Ray ray)
    {
        ray = new Ray();
        if (rect == null || camera == null)
        {
            ErrorOnce("Image picking has no image rectangle or scene camera.");
            return false;
        }
        var bounds = rect.rect;
        if (!(bounds.width > 0f) || !(bounds.height > 0f)
            || float.IsInfinity(bounds.width) || float.IsInfinity(bounds.height))
        {
            ErrorOnce("Image picking has invalid image dimensions.");
            return false;
        }
        Vector2 local;
        if (!RectTransformUtility.ScreenPointToLocalPointInRectangle(
            rect, screenPoint, uiCamera, out local))
            return false;
        var point = new Vector3(
            (local.x - bounds.xMin) / bounds.width,
            (local.y - bounds.yMin) / bounds.height, 0f);
        ray = ViewportPointToRay(camera, renderTextureType, point);
        return true;
    }

    public static Ray ScreenPointToRay(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.ScreenPointToRay(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static Vector3 WorldToScreenPoint(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.WorldToScreenPoint(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static Vector3 WorldToViewportPoint(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.WorldToViewportPoint(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static Vector3 ScreenToWorldPoint(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.ScreenToWorldPoint(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static Vector3 ViewportToWorldPoint(Camera camera, int renderTextureType, Vector3 point)
    {
        var changed = BeginQuery(camera, renderTextureType);
        try { return camera.ViewportToWorldPoint(point); }
        finally { if (changed) RestoreProjection(camera); }
    }

    public static void RestoreProjection(Camera camera)
    {
        if (camera == null)
            return;
        Matrix4x4 original;
        if (Projections.TryGetValue(camera, out original))
        {
            camera.projectionMatrix = original;
            Projections.Remove(camera);
        }
    }

    internal struct Settings
    {
        internal int Width;
        internal int Height;
        internal bool Fullscreen;
        internal bool PreserveHUDAspect;
    }

    internal static Settings Parse(string[] lines)
    {
        var settings = new Settings { PreserveHUDAspect = true };
        bool section = false, width = false, height = false, fullscreen = false, hud = false;
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#"))
                continue;
            if (line.StartsWith("[") && line.EndsWith("]"))
            {
                section = string.Equals(line, "[Resolution]", StringComparison.OrdinalIgnoreCase);
                continue;
            }
            if (!section)
                continue;
            var separator = line.IndexOf('=');
            if (separator <= 0)
                throw new FormatException("Expected key=value in [Resolution]: " + line);
            var key = line.Substring(0, separator).Trim();
            var value = line.Substring(separator + 1).Trim();
            if (string.Equals(key, "Width", StringComparison.OrdinalIgnoreCase))
            {
                if (width || !int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture,
                    out settings.Width) || settings.Width <= 0)
                    throw new FormatException("Width must be one positive integer.");
                width = true;
            }
            else if (string.Equals(key, "Height", StringComparison.OrdinalIgnoreCase))
            {
                if (height || !int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture,
                    out settings.Height) || settings.Height <= 0)
                    throw new FormatException("Height must be one positive integer.");
                height = true;
            }
            else if (string.Equals(key, "Fullscreen", StringComparison.OrdinalIgnoreCase))
            {
                if (fullscreen || !bool.TryParse(value, out settings.Fullscreen))
                    throw new FormatException("Fullscreen must be true or false, specified once.");
                fullscreen = true;
            }
            else if (string.Equals(key, "PreserveHUDAspect", StringComparison.OrdinalIgnoreCase))
            {
                if (hud || !bool.TryParse(value, out settings.PreserveHUDAspect))
                    throw new FormatException("PreserveHUDAspect must be true or false, specified once.");
                hud = true;
            }
            else
                throw new FormatException("Unknown [Resolution] key: " + key);
        }
        if (!width || !height || !fullscreen)
            throw new FormatException("[Resolution] requires Width, Height and Fullscreen.");
        return settings;
    }

    public static void Configure(ref int width, ref int height, ref bool windowed)
    {
        var path = Path.Combine(Path.GetDirectoryName(Application.dataPath), "AI1_ARFix.ini");
        try
        {
            var settings = Parse(File.ReadAllLines(path));
            width = settings.Width;
            height = settings.Height;
            windowed = !settings.Fullscreen;
            PreserveHUDAspect = settings.PreserveHUDAspect;
            Debug.Log("[AI1_ARFix] INI resolution requested: " + width + "x" + height
                + ", fullscreen=" + settings.Fullscreen + ", PreserveHUDAspect=" + PreserveHUDAspect);
        }
        catch (IOException error)
        {
            Debug.LogError("[AI1_ARFix] Cannot read " + path + ": " + error.Message
                + ". Keeping launcher resolution.");
        }
        catch (UnauthorizedAccessException error)
        {
            Debug.LogError("[AI1_ARFix] Cannot access " + path + ": " + error.Message
                + ". Keeping launcher resolution.");
        }
        catch (FormatException error)
        {
            Debug.LogError("[AI1_ARFix] Invalid " + path + ": " + error.Message
                + ". Keeping launcher resolution.");
        }
    }
}

[DefaultExecutionOrder(-32000)]
public sealed class AI1CursorAspect : MonoBehaviour
{
    private Vector3 original;
    private bool applied;

    public void Update()
    {
        Restore();
    }

    public void LateUpdate()
    {
        if (!AI1Resolution.PreserveHUDAspect || transform.parent == null)
            return;
        var inherited = transform.parent.lossyScale;
        var correction = AI1Resolution.HUDScale(Math.Abs(inherited.x), Math.Abs(inherited.y));
        original = transform.localScale;
        transform.localScale = new Vector3(
            original.x * correction.x, original.y * correction.y, original.z);
        applied = true;
    }

    public void OnDisable()
    {
        Restore();
    }

    private void Restore()
    {
        if (!applied)
            return;
        transform.localScale = original;
        applied = false;
    }
}
