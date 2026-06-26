# GIT CHEAT-SHEET — raspi-cognitive-adas (ฉบับเฉพาะ repo นี้)

> เปิดไฟล์นี้เวลาจะอัปงานขึ้น GitHub แล้วลืมว่าทำยังไง — ไม่ต้องจำ ก็อปทีละบรรทัดได้เลย
> remote = https://github.com/Triphet-DM/raspi-cognitive-adas.git

---

## 0. โมเดลความคิด (git = การเซฟเกม)

| คำสั่ง | จริงๆ มันคือ |
|---|---|
| `git add <ไฟล์>` | หยิบของใส่กระเป๋า (เลือกว่าจะเซฟอะไร) |
| `git commit -m "..."` | กดเซฟ → ได้ save point 1 อัน (อยู่ในเครื่อง) |
| `git push origin <branch>` | อัปเซฟขึ้นคลาวด์ (GitHub) |
| `git status -s` | ดูว่าตอนนี้มีอะไรค้าง/เปลี่ยนบ้าง |
| `git stash` | พักของที่ถืออยู่ไว้ในกล่อง เดี๋ยวมาเอาคืน |

**90% ของงานใช้แค่ 3 คำสั่ง: add → commit → push.**

---

## 1. กฎของ repo นี้ (สำคัญ — อ่านก่อน)

repo มี **2 branch ที่ประวัติไม่เชื่อมกัน (merge ข้ามกันไม่ได้)**:

- **`fix-gil` = dev** (ที่ทำงานจริงทุกวัน) — ของ internal อยู่ที่นี่อย่างเดียว
- **`main` = หน้าสาธารณะ** (GitHub โชว์หน้านี้ + GitHub Pages เสิร์ฟจากนี่)

**อะไรอยู่ branch ไหน:**

| ของ | ไป branch ไหน |
|---|---|
| README, `README_assets/`, `docs/`, `ARCHITECTURE_VIEWER.html` | **public → main** (และ fix-gil ด้วยถ้าอยากให้ตรงกัน) |
| `debug_reports/` (PROJECT_STATUS, session reports, ไฟล์นี้), `architecture_issues` | **internal → fix-gil เท่านั้น ห้ามขึ้น main** |
| code ใน `version_2.2_cognitive_architecture/` | ทั้งคู่ (แต่ปกติแก้ที่ fix-gil ก่อน) |

**ไฟล์ที่ git มองไม่เห็น (gitignore)** — ต้องย้ายออกมาก่อนถึงจะ track ได้:
- `demo/*` = โฟลเดอร์ raw → **รูปจะใช้ใน README ต้องก็อปไป `README_assets/` ก่อน**
- `*.wav` (เสียง), `*.ncnn.bin/.param` (โมเดล) = เก็บ local ไม่ขึ้น git

---

## 2. งานปกติ: เซฟงานที่ fix-gil (ใช้บ่อยสุด)

```bash
cd "C:/Users/triph/OneDrive/Desktop/raspi_project"
git status -s                      # ดูว่ามีอะไรเปลี่ยน
git add <ไฟล์ที่อยากเซฟ>            # อย่าใช้ git add . ถ้ามีของค้างที่ยังไม่อยากเอา
git commit -m "ข้อความสั้นๆ ว่าทำอะไร"
git push origin fix-gil
```

> เลือกเฉพาะไฟล์ที่ต้องการด้วย `git add <ชื่อไฟล์>` ทีละตัว ปลอดภัยกว่า `git add .`

---

## 3. งานยาก: อัป README / รูป / viewer ให้ขึ้นหน้าสาธารณะ (main)

> เพราะ 2 branch ประวัติไม่เชื่อมกัน เลยต้อง "ทำบน main โดยตรง" หรือ "หยิบไฟล์ข้าม"
> **กฎกันพลาด:** ก่อนสลับ branch ทุกครั้ง ต้องให้ working tree สะอาด (commit หรือ stash ของค้างก่อน)

### 3a. ถ้า README ของ main กับ fix-gil "ต่างกัน" → แก้บน main โดยตรง (ปลอดภัยสุด)
```bash
# เก็บของค้างที่ไม่เกี่ยวก่อน (เช่นงาน doc ที่ยังไม่ commit)
git stash push -- <ไฟล์ค้างที่ไม่อยากให้ติดไป>

git checkout main                  # ไป branch สาธารณะ
# ... แก้ไฟล์ / ก็อปรูปเข้า README_assets/ ตรงนี้ ...
git add <ไฟล์ที่แก้>
git commit -m "..."
git push origin main               # → GitHub Pages rebuild เองใน 1-3 นาที

git checkout fix-gil               # กลับ dev
git stash pop                      # คืนของค้างที่พักไว้
```

### 3b. ถ้าไฟล์ "เหมือนกัน" ทั้งสอง branch → หยิบไฟล์ข้ามได้ (เร็วกว่า)
```bash
git stash push -- <ไฟล์ค้าง>       # ถ้ามีของค้าง
git checkout main
git checkout fix-gil -- <ไฟล์>     # ← หยิบเฉพาะไฟล์นี้จาก fix-gil มาวางบน main
git commit -am "..."
git push origin main
git checkout fix-gil
git stash pop
```
> ⚠️ ใช้ 3b ได้เฉพาะตอนไฟล์เหมือนกัน ไม่งั้นจะ **ทับงานของ main หาย** — ไม่แน่ใจให้ใช้ 3a

**วิธีเช็คว่าไฟล์ต่างกันไหม:**
```bash
git diff --stat origin/fix-gil:README.md origin/main:README.md   # ไม่มี output = เหมือนกัน
```

---

## 4. "อ้าว ทำพลาด" — กู้คืนก่อน push

```bash
git restore <ไฟล์>                 # ทิ้งการแก้ไฟล์นั้น กลับไปเหมือน commit ล่าสุด
git restore --staged <ไฟล์>        # เอาออกจากกระเป๋า (ยกเลิก add) แต่ยังเก็บการแก้ไว้
git reset --soft HEAD~1            # ยกเลิก commit ล่าสุด แต่เก็บการแก้ไว้ (ยังไม่ push เท่านั้น)
git stash list                     # ดูว่ามีอะไรค้างใน stash
```
> ถ้า push ไปแล้วและอยากแก้ — **อย่าเพิ่งทำเอง เรียก Claude** (การ undo หลัง push อันตราย)

---

## 5. เช็คสถานะด่วน (พิมพ์เมื่อ "งง ว่าตอนนี้อยู่ตรงไหน")

```bash
git branch -vv      # อยู่ branch ไหน + sync กับ remote ไหม
git status -sb      # ไฟล์ค้าง + branch
git log --oneline -5 # 5 commit ล่าสุด
```

---

## สรุปสั้นสุด (ถ้าอ่านบรรทัดเดียว)
- **งานทั่วไป** → `add → commit → push origin fix-gil`
- **อยากให้คนเห็นบน GitHub/เว็บ** → ต้องไปลง **main** (ดูข้อ 3)
- **สับสนเมื่อไหร่** → พิมพ์ `git status -sb` ก่อน แล้วเรียก Claude ได้เลย
