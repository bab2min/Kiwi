using System.Windows;
using System.Windows.Forms;
using System.Collections.Generic;
using System.ComponentModel;
using System.Threading;
using System.IO;

namespace KiwiGui
{
    /// <summary>
    /// Interaction logic for BatchDlg.xaml
    /// </summary>
    public partial class BatchDlg : Window
    {
        public KiwiCS instKiwi;
        private BackgroundWorker worker = new BackgroundWorker();

        protected struct WorkerArgs
        {
            public List<string> fileList;
            public bool byline;
            public int topN;
            public bool formatTag;
            public string formatSep;
        }

        public BatchDlg()
        {
            InitializeComponent();
            worker.DoWork += Worker_DoWork;
            worker.RunWorkerCompleted += Worker_RunWorkerCompleted;
            worker.ProgressChanged += Worker_ProgressChanged;
            worker.WorkerReportsProgress = true;
            worker.WorkerSupportsCancellation = true;
        }

        private void Worker_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            Prg.Value = e.ProgressPercentage;
        }

        private void Worker_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            // First, handle the case where an exception was thrown.
            if (e.Error != null)
            {
                System.Windows.MessageBox.Show(e.Error.Message);
            }
            else if (e.Cancelled)
            {
                
            }
            else
            {
                System.Windows.MessageBox.Show("일괄 처리 완료");
            }
            StopBtn.Visibility = Visibility.Collapsed;
            StartBtn.Visibility = Visibility.Visible;
            Prg.Value = 0;
        }

        private void analyzeWriteResult(string input, WorkerArgs args, string outputPath)
        {
            string[] lines = args.byline ? input.Trim().Split('\n') : new string[] { input.Trim() };
            using (StreamWriter output = new StreamWriter(outputPath))
            {
                foreach (string line in lines)
                {
                    if (line.Length == 0) continue;
                    var res = instKiwi.analyze(line.Trim(), args.topN);
                    string ret = "";
                    foreach(var r in res)
                    {
                        foreach(var m in r.morphs)
                        {
                            if (ret.Length > 0) ret += args.formatSep;
                            ret += m.Item1;
                            if (args.formatTag) ret += "/" + m.Item2;
                        }
                    }
                    output.WriteLine(line.Trim());
                    output.WriteLine(ret);
                }
            }
        }

        private void Worker_DoWork(object sender, DoWorkEventArgs e)
        {
            WorkerArgs args = (WorkerArgs)e.Argument;
            worker.ReportProgress(1);
            int n = 0;
            foreach(string path in args.fileList)
            {
                n++;
                if(worker.CancellationPending)
                {
                    e.Cancel = true;
                    return;
                }
                analyzeWriteResult(MainWindow.GetFileText(path), args, path + ".tagged");
                worker.ReportProgress((int)(n * 100.0 / args.fileList.Count));
            }
        }

        private void AddBtn_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog ofd = new OpenFileDialog();
            ofd.Filter = "텍스트 파일|*.txt|모든 파일|*.*";
            ofd.Title = "분석할 텍스트 파일 열기";
            ofd.Multiselect = true;
            if (ofd.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;
            foreach(string name in ofd.FileNames)
            {
                FileList.Items.Add(name);
            }
            StartBtn.IsEnabled = true;
        }

        private void AddFolderBtn_Click(object sender, RoutedEventArgs e)
        {
            FolderBrowserDialog fd = new FolderBrowserDialog();
            if (fd.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;
            FileList.Items.Add(fd.SelectedPath);
        }

        private void DelBtn_Click(object sender, RoutedEventArgs e)
        {
            List<string> sel = new List<string>();
            foreach(string i in FileList.SelectedItems)
            {
                sel.Add(i);
            }

            for (int n = FileList.Items.Count - 1; n >= 0; --n)
            {
                string v = FileList.Items[n].ToString();
                if (sel.IndexOf(v) >= 0)
                {
                    FileList.Items.RemoveAt(n);
                }
            }
            StartBtn.IsEnabled = FileList.Items.Count > 0;
        }

        private void StartBtn_Click(object sender, RoutedEventArgs e)
        {
            StopBtn.Visibility = Visibility.Visible;
            StartBtn.Visibility = Visibility.Collapsed;
            WorkerArgs args;
            args.fileList = new List<string>();
            foreach (string i in FileList.Items)
            {
                var attr = File.GetAttributes(i);
                if(Directory.Exists(i))
                {
                    foreach (string j in Directory.GetFiles(i)) args.fileList.Add(j);
                }
                else if(File.Exists(i)) args.fileList.Add(i);
            }
            args.byline = TypeCmb.SelectedIndex == 0;
            args.topN = TopNCmb.SelectedIndex + 1;
            args.formatTag = FormatCmb.SelectedIndex % 2 == 1;
            args.formatSep = FormatCmb.SelectedIndex / 2 == 1 ? " + " : "\t";
            worker.RunWorkerAsync(args);
        }

        private void StopBtn_Click(object sender, RoutedEventArgs e)
        {
            StopBtn.Visibility = Visibility.Collapsed;
            StartBtn.Visibility = Visibility.Visible;
            worker.CancelAsync();
            Prg.Value = 0;
        }

        private void FileList_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            DelBtn.IsEnabled = FileList.SelectedIndex >= 0;
        }
    }
}
