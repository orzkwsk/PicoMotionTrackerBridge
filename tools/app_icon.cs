using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using Microsoft.Win32;

namespace PicoOTBridge
{
    internal static class AppIcon
    {
        internal static bool IsLightTaskbar()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", false))
                {
                    return key == null || Convert.ToInt32(key.GetValue("SystemUsesLightTheme", 1)) != 0;
                }
            }
            catch
            {
                return true;
            }
        }

        internal static Icon LoadTrayIcon(bool lightTaskbar, Size size)
        {
            string resource = lightTaskbar ? "PicoOTBridge.Icons.Black" : "PicoOTBridge.Icons.White";
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(resource))
            {
                if (stream == null)
                {
                    throw new InvalidOperationException("Embedded PICO icon was not found: " + resource);
                }
                using (var icon = new Icon(stream, size))
                {
                    return (Icon)icon.Clone();
                }
            }
        }
    }
}
