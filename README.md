# Mini-UnionFS (Cloud Computing Project)

## 📌 Overview

This project implements a simplified **Union File System (UnionFS)** using **FUSE (Filesystem in Userspace)**.

It mimics how container systems like Docker manage layered file systems:

* A **read-only lower layer** (base image)
* A **read-write upper layer** (container layer)

These layers are merged into a single virtual filesystem.

---

## 🚀 Features

### 1. Layer Stacking (Directory Union)

* Combines `lower_dir` and `upper_dir`
* If a file exists in both → **upper layer takes precedence**

---

### 2. Copy-on-Write (CoW)

* When modifying a file from `lower_dir`:

  * File is copied to `upper_dir`
  * Modifications happen only in `upper_dir`
* Ensures original data remains unchanged

---

### 3. Whiteout Mechanism (Deletion Handling)

* Deleting a file from lower layer:

  * Creates `.wh.<filename>` in `upper_dir`
  * Hides the file from merged view

---

### 4. Supported Operations

* `getattr`
* `readdir`
* `open`
* `read`
* `write`
* `unlink`

---

## 🛠️ Requirements

* Ubuntu 22.04 (or similar Linux), Or wsl in windows
* FUSE3

Install dependencies:

```bash
sudo apt update
sudo apt install libfuse3-dev fuse3
```

---

## ⚙️ Build Instructions

```bash
make
```

---

## 📂 Setup Test Environment

```bash
make setup
```

This creates:

```
unionfs_test_env/
├── lower/
├── upper/
├── mnt/
```

---

## ▶️ Running the Filesystem

```bash
sudo ./mini_unionfs unionfs_test_env/lower unionfs_test_env/upper unionfs_test_env/mnt -f
```

---

## 🧪 Testing

### Test 1: Visibility

```bash
cat unionfs_test_env/mnt/base.txt
```

---

### Test 2: Copy-on-Write

```bash
echo "new data" >> unionfs_test_env/mnt/base.txt
```

Check:

```bash
cat unionfs_test_env/upper/base.txt   # contains new data
cat unionfs_test_env/lower/base.txt   # unchanged
```

---

### Test 3: Whiteout

```bash
rm unionfs_test_env/mnt/delete_me.txt
```

Check:

```bash
ls unionfs_test_env/upper/.wh.delete_me.txt
```

---

## 🧹 Cleanup

```bash
make clean
```

---

## 📌 Notes

* `mnt/` is a mount point and may not appear in Git (empty folder)
* Create manually if missing:

```bash
mkdir -p unionfs_test_env/mnt
```

---

## 👨‍💻 Authors
* Nishita Singh [ohnonyx](https://github.com/ohnonyx) (handled Read & Merge Logic)
* Nithya Prashaanthi. R [nithya-1385](https://github.com/nithya-1385) (handled Copy-on-Write Implementation)
* Nithya Anantharaman [nithya049](http://github.com/nithya049) (handled Whiteout Mechanism)

---

## 📚 References

* FUSE Documentation
* UnionFS / OverlayFS concepts (Linux)
