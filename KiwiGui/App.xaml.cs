using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Deployment.Application;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using GoogleAnalytics;

namespace KiwiGui
{
    internal class PlatformInfoProvider : IPlatformInfoProvider
    {
        public string AnonymousClientId { get; set; }

        public int? ScreenColors { get; set; }

        public Dimensions ScreenResolution { get; set; }
        public string UserAgent { get; set; }
        public string UserLanguage { get; set; }
        public Dimensions ViewPortResolution { get; set; }

        Dimensions? IPlatformInfoProvider.ScreenResolution { get; }

        Dimensions? IPlatformInfoProvider.ViewPortResolution { get; }

        public event EventHandler ScreenResolutionChanged;
        public event EventHandler ViewPortResolutionChanged;

        public void OnTracking()
        {
            throw new NotImplementedException();
        }
    }

    public class AnalitycsMonitor
    {
        private Tracker tracker;

        public AnalitycsMonitor()
        {
            CreateGoogleTracker();
        }

        public void CreateGoogleTracker()
        {
            Dimensions screenDimension = new Dimensions((int)SystemParameters.PrimaryScreenWidth, (int)SystemParameters.PrimaryScreenHeight);
            CultureInfo ci = CultureInfo.CurrentUICulture;

            var trackerManager = new TrackerManager(new PlatformInfoProvider()
            {
                AnonymousClientId = PlatformCheck.GetUniqueComputerID(), // Random UUID
                ScreenResolution = screenDimension,
                UserAgent = PlatformCheck.GetSystemInfo().osName,
                UserLanguage = ci.Name,
                ViewPortResolution = screenDimension
            });

            tracker = trackerManager.CreateTracker("UA-120517126-1");
            tracker.AppName = "Kiwi";
            tracker.AppId = "KiwiGui";
            tracker.AppVersion = App.getRunningVersion().ToString();
            tracker.ScreenResolution = screenDimension;
        }

        public void TrackScreenView(string screenName)
        {
            var data = HitBuilder.CreateScreenView(screenName).Build();
            tracker.Send(data);
        }

        public void TrackAtomicFeature(string category, string action, string label = "", long value = 1)
        {
            var data = HitBuilder.CreateCustomEvent(category, action, label, value).Build();
            tracker.Send(data);
        }

        public void TrackError(Exception exception)
        {
            Regex patFun = new Regex("KiwiGui\\.[a-zA-Z0-9().?]+");
            Match m = patFun.Match(exception.StackTrace);
            var data = HitBuilder.CreateException(exception.GetType().ToString() + ":" + exception.Message + " : " + m?.Groups[0], true).Build();
            tracker.Send(data);
        }
    }


    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        public static AnalitycsMonitor monitor = new AnalitycsMonitor();

        public App() : base()
        {
            this.Dispatcher.UnhandledException += OnDispatcherUnhandledException;
        }

        public static Version getRunningVersion()
        {
            try
            {
                return ApplicationDeployment.CurrentDeployment.CurrentVersion;
            }
            catch (Exception)
            {
                return Assembly.GetExecutingAssembly().GetName().Version;
            }
        }

        void OnDispatcherUnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
        {
            List<Exception> eList = new List<Exception>();
            eList.Add(e.Exception);
            while (eList.Last().InnerException != null) eList.Add(eList.Last().InnerException);
            eList.Reverse();
            App.monitor.TrackError(eList[0]);
            string errorMessage = "An unhandled exception occurred\n" + String.Join("\n", eList.Select(ex => ex.Message));
            MessageBox.Show(errorMessage, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
