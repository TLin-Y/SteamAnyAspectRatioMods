using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

public static class PWAATResolution
{
    private static bool loaded;
    private static int width;
    private static int height;

    private static void Load()
    {
        if (loaded)
            return;
        loaded = true;
        try
        {
            var path = Path.Combine(Path.GetDirectoryName(Application.dataPath), "PWAAT_ARFix.ini");
            int w = 0, h = 0;
            foreach (var raw in File.ReadAllLines(path))
            {
                var line = raw.Trim();
                if (line.StartsWith(";") || line.StartsWith("#"))
                    continue;
                int eq = line.IndexOf('=');
                if (eq <= 0)
                    continue;
                var key = line.Substring(0, eq).Trim();
                int value;
                if (!int.TryParse(line.Substring(eq + 1).Trim(), out value))
                    continue;
                if (string.Equals(key, "Width", StringComparison.OrdinalIgnoreCase))
                    w = value;
                else if (string.Equals(key, "Height", StringComparison.OrdinalIgnoreCase))
                    h = value;
            }
            if (w > 0 && h > 0)
            {
                width = w;
                height = h;
            }
        }
        catch (Exception)
        {
        }
    }

    public static Resolution[] GetResolutions()
    {
        var list = new List<Resolution>(Screen.resolutions);
        Load();
        if (width > 0 && !list.Exists(r => r.width == width && r.height == height))
        {
            var added = new Resolution();
            added.width = width;
            added.height = height;
            added.refreshRate = Screen.currentResolution.refreshRate;
            list.Add(added);
            list.Sort((a, b) => a.width != b.width ? a.width.CompareTo(b.width) : a.height.CompareTo(b.height));
        }
        return list.ToArray();
    }

    public static void ApplyStartup(ref uint resolutionWidth, ref uint resolutionHeight)
    {
        Load();
        if (width <= 0)
            return;
        resolutionWidth = (uint)width;
        resolutionHeight = (uint)height;
    }

    public static void HideFrame(Component frame)
    {
        try
        {
            var host = frame.transform.parent != null ? frame.transform.parent.gameObject : frame.gameObject;
            if (host.GetComponent<PWAATSceneCamera>() == null)
                host.AddComponent<PWAATSceneCamera>();
        }
        catch (Exception)
        {
        }
        frame.gameObject.SetActive(false);
    }
}

public class PWAATSceneCamera : MonoBehaviour
{
    private const float ReferenceAspect = 16f / 9f;

    private static bool IsScene(Transform t)
    {
        for (var p = t.parent; p != null; p = p.parent)
            if (p.name == "adventure")
                return true;
        return false;
    }

    private void LateUpdate()
    {
        foreach (var c in Camera.allCameras)
        {
            if (!c.orthographic || c.targetTexture != null || c.rect.width < 0.999f || c.rect.height < 0.999f
                || !IsScene(c.transform))
                continue;
            c.ResetProjectionMatrix();
            float a = c.aspect;
            if (a <= 0f || a >= ReferenceAspect - 0.0001f)
                continue;
            float h = c.orthographicSize * a / ReferenceAspect;
            c.projectionMatrix = Matrix4x4.Ortho(-h * a, h * a, -h, h, c.nearClipPlane, c.farClipPlane);
        }
    }
}
