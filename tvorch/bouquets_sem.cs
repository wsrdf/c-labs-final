using System;
using System.Threading;

class BouquetsSemaphore
{
    static readonly string[] SupplierGives = { "розы и фиалки", "пионы и розы", "пионы и фиалки" };
    static readonly string[] MakerHas = { "пионы", "фиалки", "розы" };

    static int onTable = -1;

    // Вход в критическую секцию. (1, 1) - начальное значение 1 (свободно),
    // максимум 1 (двоичный семафор)
    static readonly SemaphoreSlim mutex = new SemaphoreSlim(1, 1);
    // "Стол пуст": пока составитель не потребил цветы, новый поставщик не может их класть
    static readonly SemaphoreSlim tableEmpty = new SemaphoreSlim(1, 1);
    // Сигнал составителю i: на столе лежат нужные ему ресурсы.
    // Начальное значение 0 - в начале цветов на столе нет
    static readonly SemaphoreSlim[] makerSem =
        { new SemaphoreSlim(0, 1), new SemaphoreSlim(0, 1), new SemaphoreSlim(0, 1) };
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
        var rnd = new Random(i * 100 + Environment.TickCount);
        while (true)
        {
            // Поставщик "ходит за товаром" - случайная пауза
            Thread.Sleep(rnd.Next(300, 800));
            // Ждём, пока составитель потребит цветы предыдущего поставщика
            tableEmpty.Wait();
            Log(name, "пытается войти в критическую секцию");
            mutex.Wait(); // начало КС
            Log(name, "ВОШЁЛ в критическую секцию, кладёт: " + SupplierGives[i]);
            onTable = i; // выкладываем свой набор на стол
            Log(name, "вышел из критической секции");
            mutex.Release(); // конец КС
            makerSem[i].Release(); // сообщаем составителю i: цветы на столе
        }
    }

    static void Maker(object o)
    {
        int i = (int)o;
        string name = "S" + (i + 1);
        var rnd = new Random(i * 200 + Environment.TickCount);
        while (true)
        {
            Log(name, "готов составить букет (есть " + MakerHas[i] + "), ждёт ресурсы");
            makerSem[i].Wait(); // спим, пока поставщик i не положит цветы
            Log(name, "пытается войти в критическую секцию");
            mutex.Wait(); // начало КС
            Log(name, "ВОШЁЛ в критическую секцию, забирает ресурсы со стола");
            onTable = -1; // ресурсы потреблены
            Log(name, "вышел из критической секции, составляет букет");
            mutex.Release(); // конец КС
            tableEmpty.Release();
            Thread.Sleep(rnd.Next(500, 1200));
            Log(name, "букет составлен");
        }
    }

    static void Main()
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;
        Console.WriteLine("Составители букетов (семафоры). Ctrl+C - выход.");
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
