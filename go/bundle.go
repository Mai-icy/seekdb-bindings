package seekdb

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

func unpackBundle() (string, error) {
	archive := platformBundle()
	if len(archive) == 0 {
		return "", fmt.Errorf("seekdb has no native bundle for this platform")
	}
	checksum := sha256.Sum256(archive)
	cacheRoot, err := os.UserCacheDir()
	if err != nil {
		return "", fmt.Errorf("find cache directory: %w", err)
	}
	root := filepath.Join(cacheRoot, "seekdb-go")
	if err := os.MkdirAll(root, 0700); err != nil {
		return "", fmt.Errorf("create native cache: %w", err)
	}
	target := filepath.Join(root, hex.EncodeToString(checksum[:]))
	if bundleReady(target) {
		return target, nil
	}
	temp, err := os.MkdirTemp(root, ".extract-")
	if err != nil {
		return "", fmt.Errorf("create extraction directory: %w", err)
	}
	defer os.RemoveAll(temp)
	if err := extractArchive(temp, archive); err != nil {
		return "", err
	}
	if !bundleReady(temp) {
		return "", errors.New("native bundle lacks libseekdb or seekdb executable")
	}
	if err := os.Rename(temp, target); err != nil && !bundleReady(target) {
		return "", fmt.Errorf("install native bundle: %w", err)
	}
	return target, nil
}

func bundleReady(dir string) bool {
	for _, name := range []string{platformLibrary(), platformExecutable(), "seekdb.bin"} {
		if name == "" {
			return false
		}
		info, err := os.Stat(filepath.Join(dir, name))
		if err != nil || !info.Mode().IsRegular() || info.Mode().Perm()&0111 == 0 {
			return false
		}
	}
	return true
}

func extractArchive(dir string, archive []byte) error {
	gz, err := gzip.NewReader(bytes.NewReader(archive))
	if err != nil {
		return fmt.Errorf("read native bundle: %w", err)
	}
	defer gz.Close()
	tr := tar.NewReader(gz)
	for {
		header, err := tr.Next()
		if errors.Is(err, io.EOF) {
			return nil
		}
		if err != nil {
			return fmt.Errorf("read native bundle entry: %w", err)
		}
		name := header.Name
		if name == "" || name == "." || name == ".." || strings.ContainsAny(name, `/\\`) || header.Typeflag != tar.TypeReg {
			return fmt.Errorf("invalid native bundle entry %q", name)
		}
		mode := os.FileMode(header.Mode) & 0777
		file, err := os.OpenFile(filepath.Join(dir, name), os.O_WRONLY|os.O_CREATE|os.O_EXCL, mode)
		if err != nil {
			return fmt.Errorf("create native file %q: %w", name, err)
		}
		_, copyErr := io.Copy(file, tr)
		closeErr := file.Close()
		if copyErr != nil {
			return fmt.Errorf("extract native file %q: %w", name, copyErr)
		}
		if closeErr != nil {
			return fmt.Errorf("close native file %q: %w", name, closeErr)
		}
	}
}
