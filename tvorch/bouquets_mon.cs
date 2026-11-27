using System;
using System.Threading;

class BouquetsMonitor
{
    static readonly string[] SupplierGives = { "розы и фиалки", "пионы и розы", "пионы и фиалки" };
    static readonly string[] MakerHas = { "пионы", "фиалки", "розы" };
    // монитор - захват блокировки критической секции
    static readonly object table = new object();
    // cостояние стола, читается и меняется только внутри монитора table
    static int onTable = -1; // номер поставщика, чьи ресурсы лежат на столе; -1 - стол пуст
    static readonly object consoleLock = new object();

    static void Log(string who, string msg)
    {
        lock (consoleLock)
            Console.WriteLine("[{0:HH:mm:ss.fff}] {1}: {2}", DateTime.Now, who, msg);
    }

    static void Supplier(object o)
    {
        int i = (int)o;
        string name = "V" + (i + 1);
        var rnd = new Random(i * 100 + Environment.TickCount); // seed для каждого потока
        while (true)
        {
            Thread.Sleep(rnd.Next(300, 800));
            Log(name, "пытается войти в критическую секцию");
            Monitor.Enter(table);
            try // начало КС
            {
                while (onTable != -1)
                    Monitor.Wait(table);
                Log(name, "ВОШЁЛ в критическую секцию, кладёт: " + SupplierGives[i]);
                onTable = i;
                Monitor.PulseAll(table);
                Log(name, "вышел из критической секции");
            }
            finally { Monitor.Exit(table); } // конец КС
        }
    }

    // Поток составителя. o - номер составителя (0, 1, 2)
    static void Maker(object o)
    {
        int i = (int)o;
        string name = "S" + (i + 1);
        var rnd = new Random(i * 200 + Environment.TickCount);
        while (true)
        {
            Log(name, "готов составить букет (есть " + MakerHas[i] + "), ждёт ресурсы");
            Log(name, "пытается войти в критическую секцию");
            Monitor.Enter(table);
            try // начало КС
            {
                while (onTable != i)
                    Monitor.Wait(table);
                Log(name, "ВОШЁЛ в критическую секцию, забирает ресурсы со стола");
                onTable = -1;
                Monitor.PulseAll(table); 
                Log(name, "вышел из критической секции, составляет букет");
            }
            finally { Monitor.Exit(table); } // конец КС
            Thread.Sleep(rnd.Next(500, 1200));
            Log(name, "букет составлен");
        }
    }

    static void Main()
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;
        Console.WriteLine("Составители букетов (мониторы). Ctrl+C - выход.");
        for (int i = 0; i < 3; i++)
        {
            var s = new Thread(Maker) { IsBackground = true };
            s.Start(i);
            var v = new Thread(Supplier) { IsBackground = true };
            v.Start(i);
        }
        Thread.Sleep(Timeout.Infinite);
    }
}
