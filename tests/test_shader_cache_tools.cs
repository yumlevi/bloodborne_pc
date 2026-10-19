// SPDX-License-Identifier: GPL-2.0-or-later
// Compile against the portable launcher assembly to test cache import/export without starting UI.
using System;
using System.IO;
using System.IO.Compression;
using System.Text;
using BbportPortable;

class ShaderCacheTests {
    static void Require(bool value, string message) {
        if (!value) throw new Exception(message);
    }

    static void ExpectInvalid(Action action, string message) {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new Exception(message);
    }

    static int Main() {
        try { Run(); return 0; }
        catch (Exception error) {
            Console.Error.WriteLine(error.GetType().FullName + ": " + error.Message);
            Console.Error.WriteLine(error.StackTrace);
            return 1;
        }
    }

    static void Run() {
        string testParent = Path.GetFullPath(Path.Combine(Environment.CurrentDirectory, "dist"));
        string root = Path.GetFullPath(Path.Combine(testParent,
            "shader-test-" + Guid.NewGuid().ToString("N")));
        string source = Path.Combine(root, "source");
        string target = Path.Combine(root, "target");
        string mismatch = Path.Combine(root, "mismatch");
        string archive = Path.Combine(root, "cache.zip");
        try {
            Directory.CreateDirectory(ShaderCacheTools.DirectoryPath(source));
            Directory.CreateDirectory(ShaderCacheTools.DirectoryPath(target));
            Directory.CreateDirectory(ShaderCacheTools.DirectoryPath(mismatch));
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(source), "profile.bin"), new byte[] { 1, 2, 3, 4 });
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(source), "g_0x1234.key"), new byte[] { 5, 6 });
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(source), "0x1234_0.spv"), new byte[] { 7, 8 });
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(source), "incomplete.tmp"), new byte[] { 9 });
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(target), "profile.bin"), new byte[] { 1, 2, 3, 4 });
            File.WriteAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(mismatch), "profile.bin"), new byte[] { 4, 3, 2, 1 });

            Require(ShaderCacheTools.Export(source, archive, "1.7.7") == 3, "exported wrong file count");
            Require(ShaderCacheTools.Export(source, archive, "1.7.7") == 3, "re-export failed to replace ZIP");
            ExpectInvalid(delegate { ShaderCacheTools.Export(source,
                Path.Combine(ShaderCacheTools.DirectoryPath(source), "profile.bin"), "1.7.7"); },
                "export could overwrite a cache file");
            Require(ShaderCacheTools.Import(target, archive, "1.7.7") == 2, "imported wrong file count");
            Require(ShaderCacheTools.Import(target, archive, "1.7.7") == 0, "reimport overwrote existing files");
            Require(File.ReadAllBytes(Path.Combine(ShaderCacheTools.DirectoryPath(target), "g_0x1234.key"))[0] == 5,
                "imported bytes differ");
            Require(!File.Exists(Path.Combine(ShaderCacheTools.DirectoryPath(target), "incomplete.tmp")),
                "temporary file was imported");
            ExpectInvalid(delegate { ShaderCacheTools.Import(mismatch, archive, "1.7.7"); },
                "mismatched GPU profile was accepted");
            ExpectInvalid(delegate { ShaderCacheTools.Import(target, archive, "1.7.8"); },
                "mismatched launcher version was accepted");

            string unsafeArchive = Path.Combine(root, "unsafe.zip");
            using (ZipArchive zip = ZipFile.Open(unsafeArchive, ZipArchiveMode.Create)) {
                using (StreamWriter writer = new StreamWriter(zip.CreateEntry("bbport-cache-manifest.txt").Open(),
                    new UTF8Encoding(false))) writer.Write("bbport-shader-cache-v1\n1.7.7\nCUSA03173\n");
                using (Stream output = zip.CreateEntry("CUSA03173/profile.bin").Open())
                    output.Write(new byte[] { 1, 2, 3, 4 }, 0, 4);
                using (Stream output = zip.CreateEntry("CUSA03173/../outside.key").Open()) output.WriteByte(1);
            }
            ExpectInvalid(delegate { ShaderCacheTools.Import(target, unsafeArchive, "1.7.7"); },
                "archive path traversal was accepted");
            Require(!File.Exists(Path.Combine(root, "outside.key")), "archive escaped cache directory");
            Console.WriteLine("Shader cache tests passed");
        } finally {
            if (root.StartsWith(testParent + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) &&
                Path.GetFileName(root).StartsWith("shader-test-", StringComparison.Ordinal) &&
                Directory.Exists(root)) Directory.Delete(root, true);
        }
    }
}
