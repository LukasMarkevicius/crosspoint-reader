const canvas = document.getElementById("previewCanvas");
const ctx = canvas.getContext("2d");

const controls = {
  mode: document.getElementById("mode"),
  variant: document.getElementById("variant"),
  date: document.getElementById("date"),
  weekStart: document.getElementById("weekStart"),
  birthDate: document.getElementById("birthDate"),
  lifespan: document.getElementById("lifespan"),
  gridShape: document.getElementById("gridShape"),
  accentStyle: document.getElementById("accentStyle"),
  todayButton: document.getElementById("todayButton"),
  downloadButton: document.getElementById("downloadButton"),
  previewTitle: document.getElementById("previewTitle"),
  previewMeta: document.getElementById("previewMeta"),
};

const variantChips = Array.from(document.querySelectorAll(".variant-chip"));

const MONTH_NAMES = [
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December",
];

const WEEKDAY_NAMES = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];

function pad(value) {
  return String(value).padStart(2, "0");
}

function toInputDate(date) {
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
}

function parseDateInput(value) {
  const [year, month, day] = value.split("-").map(Number);
  return new Date(year, month - 1, day, 12, 0, 0, 0);
}

function getState() {
  const selectedDate = parseDateInput(controls.date.value);

  return {
    mode: controls.mode.value,
    variant: controls.variant.value,
    now: selectedDate,
    weekStart: controls.weekStart.value,
    birthDate: parseDateInput(controls.birthDate.value),
    lifespan: Math.max(1, Number(controls.lifespan.value) || 85),
    gridShape: controls.gridShape.value,
    accentStyle: controls.accentStyle.value,
  };
}

function fillPaper(base = "#f7f2e8") {
  ctx.fillStyle = base;
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.save();
  ctx.globalAlpha = 0.08;
  for (let y = 0; y < canvas.height; y += 4) {
    ctx.fillStyle = y % 8 === 0 ? "#d2c8b6" : "#ffffff";
    ctx.fillRect(0, y, canvas.width, 1);
  }
  ctx.restore();
}

function drawFrame() {
  ctx.strokeStyle = "#181512";
  ctx.lineWidth = 2;
  ctx.strokeRect(18, 18, canvas.width - 36, canvas.height - 36);
}

function setFont(size, family, weight = "400") {
  ctx.font = `${weight} ${size}px ${family}`;
}

function centeredText(text, y, size, family, weight = "400", color = "#181512") {
  setFont(size, family, weight);
  ctx.fillStyle = color;
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  ctx.fillText(text, canvas.width / 2, y);
}

function textAtX(text, x, y, size, family, weight = "400", color = "#181512") {
  setFont(size, family, weight);
  ctx.fillStyle = color;
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  ctx.fillText(text, x, y);
}

function measureInkBounds(text, size, family, weight = "400") {
  setFont(size, family, weight);
  const metrics = ctx.measureText(text);
  const left = metrics.actualBoundingBoxLeft ?? 0;
  const right = metrics.actualBoundingBoxRight ?? 0;
  const ascent = metrics.actualBoundingBoxAscent ?? size * 0.72;
  const descent = metrics.actualBoundingBoxDescent ?? size * 0.2;
  return {
    width: left + right,
    height: ascent + descent,
    centerX: (right - left) / 2,
    centerY: (descent - ascent) / 2,
  };
}

function leftText(text, x, y, size, family, weight = "400", color = "#181512") {
  setFont(size, family, weight);
  ctx.fillStyle = color;
  ctx.textAlign = "left";
  ctx.textBaseline = "top";
  ctx.fillText(text, x, y);
}

function rightText(text, x, y, size, family, weight = "400", color = "#181512") {
  setFont(size, family, weight);
  ctx.fillStyle = color;
  ctx.textAlign = "right";
  ctx.textBaseline = "top";
  ctx.fillText(text, x, y);
}

function getWeekHeaders(weekStart) {
  return weekStart === "sunday" ? ["S", "M", "T", "W", "T", "F", "S"] : ["M", "T", "W", "T", "F", "S", "S"];
}

function getCalendarStartColumn(date, weekStart) {
  const monthStart = new Date(date.getFullYear(), date.getMonth(), 1);
  const day = monthStart.getDay();
  return weekStart === "sunday" ? day : (day + 6) % 7;
}

function drawCalendarGrid(bounds, date, weekStart, accentStyle, variant) {
  const headers = getWeekHeaders(weekStart);
  const startColumn = getCalendarStartColumn(date, weekStart);
  const monthDays = new Date(date.getFullYear(), date.getMonth() + 1, 0).getDate();
  const rows = 6;
  const cellWidth = bounds.width / 7;
  const headerY = bounds.y;
  const gridY = headerY + 36;
  const cellHeight = (bounds.height - 28) / rows;

  for (let col = 0; col < 7; col++) {
    const x = bounds.x + col * cellWidth;
    textAtX(headers[col], x + cellWidth / 2, headerY, 18, '"Courier New"', "700", "#666056");
  }

  for (let day = 1; day <= monthDays; day++) {
    const offset = startColumn + day - 1;
    const row = Math.floor(offset / 7);
    const col = offset % 7;
    const x = bounds.x + col * cellWidth;
    const y = gridY + row * cellHeight;
    const isToday = day === date.getDate();
    const centerX = x + cellWidth / 2;
    const centerY = y + cellHeight / 2 + 6;

    const circleCenterX = isToday ? centerX + 2 : centerX;

    const circleCenterY = isToday ? centerY - 1 : centerY;

    if (isToday) {
      ctx.beginPath();
      ctx.fillStyle = "#191613";
      ctx.arc(circleCenterX, circleCenterY, 22, 0, Math.PI * 2);
      ctx.fill();
    }

    const color = isToday ? "#f7f2e8" : "#191613";
    const textCenterX = isToday ? centerX + 2 : centerX;
    const textY = isToday ? centerY - 15 : centerY - 18;
    textAtX(
      String(day),
      textCenterX,
      textY,
      variant === "poster" ? 24 : 22,
      "Georgia",
      isToday ? "700" : "400",
      color
    );
  }
}

function drawFirmwareCalendarGrid(date) {
  const monthStartWeekday = new Date(date.getFullYear(), date.getMonth(), 1).getDay();
  const startColumn = (monthStartWeekday + 6) % 7;
  const monthDays = new Date(date.getFullYear(), date.getMonth() + 1, 0).getDate();
  const contentLeft = 40;
  const contentRight = canvas.width - 40;
  const contentWidth = contentRight - contentLeft;
  const headersY = 150;
  const gridTop = 188;
  const gridBottom = canvas.height - 56;
  const gridHeight = gridBottom - gridTop;
  const cellWidth = Math.floor(contentWidth / 7);
  const cellHeight = gridHeight / 6;

  for (let col = 0; col < 7; col++) {
    const centerX = contentLeft + col * cellWidth + cellWidth / 2;
    textAtX("MTWTFSS"[col], centerX, headersY, 18, '"Courier New"', "700", "#666056");
  }

  for (let day = 1; day <= monthDays; day++) {
    const offset = startColumn + day - 1;
    const row = Math.floor(offset / 7);
    const col = offset % 7;
    if (row >= 6) break;

    const cellX = contentLeft + col * cellWidth;
    const cellY = gridTop + row * cellHeight;
    const isToday = day === date.getDate();
    const centerX = cellX + cellWidth / 2;
    const centerY = cellY + cellHeight / 2 + 2;

    const highlightCenterX = centerX + 2;
    const highlightCenterY = centerY + 1;

    if (isToday) {
      ctx.beginPath();
      ctx.fillStyle = "#191613";
      ctx.arc(highlightCenterX, highlightCenterY, 26, 0, Math.PI * 2);
      ctx.fill();
    }

    const inkBounds = measureInkBounds(String(day), 22, "Georgia", "400");
    textAtX(
      String(day),
      isToday ? highlightCenterX - inkBounds.centerX : centerX,
      isToday ? highlightCenterY - inkBounds.centerY - inkBounds.height / 2 : centerY - 18,
      22,
      "Georgia",
      "400",
      isToday ? "#f7f2e8" : "#191613"
    );
  }
}

function drawFirmwareCalendar(state) {
  fillPaper("#f8f4ec");
  const monthLabel = `${MONTH_NAMES[state.now.getMonth()].toUpperCase()} ${state.now.getFullYear()}`;
  const subtitle = `${WEEKDAY_NAMES[state.now.getDay()].toUpperCase()} ${state.now.getDate()}`;

  ctx.strokeStyle = "#575046";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(52, 126);
  ctx.lineTo(canvas.width - 52, 126);
  ctx.stroke();

  centeredText(monthLabel, 44, 22, '"Courier New"', "700", "#353029");
  centeredText(subtitle, 90, 16, '"Courier New"', "400", "#625b50");
  drawFirmwareCalendarGrid(state.now);
}

function drawQuietCalendar(state) {
  fillPaper("#f6f0e2");

  ctx.fillStyle = "#e8dfd1";
  roundRect(34, 38, canvas.width - 68, canvas.height - 76, 24, true, false);
  ctx.strokeStyle = "#312d28";
  ctx.lineWidth = 1.5;
  roundRect(34, 38, canvas.width - 68, canvas.height - 76, 24, false, true);

  centeredText(MONTH_NAMES[state.now.getMonth()], 72, 26, "Georgia", "700");
  centeredText(String(state.now.getFullYear()), 104, 15, '"Courier New"', "400", "#575046");
  centeredText(String(state.now.getDate()), 144, 110, "Georgia", "700");

  const weekday = WEEKDAY_NAMES[state.now.getDay()];
  centeredText(weekday, 268, 18, '"Courier New"', "700", "#595246");

  drawCalendarGrid(
    { x: 62, y: 356, width: canvas.width - 124, height: 334 },
    state.now,
    state.weekStart,
    state.accentStyle,
    "quiet"
  );
}

function drawPosterCalendar(state) {
  fillPaper("#f9f3e5");

  ctx.fillStyle = "#1d1916";
  ctx.fillRect(0, 0, canvas.width, 132);
  centeredText(MONTH_NAMES[state.now.getMonth()].toUpperCase(), 28, 22, '"Courier New"', "700", "#f9f3e5");
  centeredText(String(state.now.getFullYear()), 58, 18, '"Courier New"', "400", "#d8d2c7");

  rightText(String(state.now.getDate()), canvas.width - 38, 150, 154, "Georgia", "700", "#171310");
  leftText(WEEKDAY_NAMES[state.now.getDay()].toUpperCase(), 38, 172, 20, '"Courier New"', "700", "#5f584d");

  ctx.fillStyle = "#dfd6c6";
  ctx.fillRect(38, 284, canvas.width - 76, 2);

  drawCalendarGrid(
    { x: 38, y: 318, width: canvas.width - 76, height: 410 },
    state.now,
    state.weekStart,
    state.accentStyle,
    "poster"
  );
}

function roundRect(x, y, width, height, radius, fill, stroke) {
  ctx.beginPath();
  ctx.moveTo(x + radius, y);
  ctx.lineTo(x + width - radius, y);
  ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
  ctx.lineTo(x + width, y + height - radius);
  ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
  ctx.lineTo(x + radius, y + height);
  ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
  ctx.lineTo(x, y + radius);
  ctx.quadraticCurveTo(x, y, x + radius, y);
  ctx.closePath();
  if (fill) ctx.fill();
  if (stroke) ctx.stroke();
}

function weeksLived(now, birthDate) {
  const diffMs = now - birthDate;
  return Math.max(0, Math.floor(diffMs / (1000 * 60 * 60 * 24 * 7)));
}

function monthsLived(now, birthDate) {
  let months = (now.getFullYear() - birthDate.getFullYear()) * 12 + (now.getMonth() - birthDate.getMonth());
  if (now.getDate() < birthDate.getDate()) months -= 1;
  return Math.max(0, months);
}

function drawLifeGrid(state, variant) {
  const birthDate = state.birthDate;
  fillPaper(variant === "poster" ? "#f8f0e0" : "#f6f1e7");

  const unitLabel = state.gridShape === "weeks" ? "weeks" : "months";
  const totalUnits = state.gridShape === "weeks" ? state.lifespan * 52 : state.lifespan * 12;
  const livedUnits = state.gridShape === "weeks" ? weeksLived(state.now, birthDate) : monthsLived(state.now, birthDate);
  const safeLived = Math.min(totalUnits, livedUnits);

  if (variant === "quiet") {
    ctx.fillStyle = "#e6ddcd";
    roundRect(28, 28, canvas.width - 56, canvas.height - 56, 22, true, false);
  } else if (variant === "poster") {
    ctx.fillStyle = "#181512";
    ctx.fillRect(0, 0, canvas.width, 116);
    centeredText("LIFE GRID", 30, 28, '"Courier New"', "700", "#f6f1e7");
  } else {
    drawFrame();
  }

  const titleY = variant === "poster" ? 132 : 52;
  centeredText("Life Progress", titleY, variant === "poster" ? 24 : 28, "Georgia", "700");
  centeredText(`${safeLived.toLocaleString()} of ${totalUnits.toLocaleString()} ${unitLabel}`, titleY + 34, 16, '"Courier New"',
    "400", "#5d564c");

  const percent = totalUnits > 0 ? (safeLived / totalUnits) * 100 : 0;
  centeredText(`${percent.toFixed(1)}% lived`, titleY + 58, 18, '"Courier New"', "700", "#221f1b");

  const cols = state.gridShape === "weeks" ? 52 : 24;
  const rows = Math.ceil(totalUnits / cols);
  const gridLeft = 38;
  const gridTop = variant === "poster" ? 250 : 190;
  const gridWidth = canvas.width - 76;
  const gridHeight = canvas.height - gridTop - 72;
  const cellGap = state.gridShape === "weeks" ? 2 : 3;
  const cellWidth = (gridWidth - cellGap * (cols - 1)) / cols;
  const cellHeight = (gridHeight - cellGap * (rows - 1)) / rows;

  for (let i = 0; i < totalUnits; i++) {
    const row = Math.floor(i / cols);
    const col = i % cols;
    const x = gridLeft + col * (cellWidth + cellGap);
    const y = gridTop + row * (cellHeight + cellGap);
    const isLived = i < safeLived;

    if (isLived) {
      if (state.accentStyle === "solid") {
        ctx.fillStyle = "#191613";
      } else if (state.accentStyle === "frame") {
        ctx.fillStyle = "#d8d0c0";
      } else {
        ctx.fillStyle = "#c6bcac";
      }
      ctx.fillRect(x, y, cellWidth, cellHeight);
      if (state.accentStyle === "frame") {
        ctx.strokeStyle = "#191613";
        ctx.lineWidth = 1;
        ctx.strokeRect(x + 0.5, y + 0.5, cellWidth - 1, cellHeight - 1);
      }
    } else {
      ctx.fillStyle = "#f7f2e9";
      ctx.fillRect(x, y, cellWidth, cellHeight);
      ctx.strokeStyle = "#d2c8b8";
      ctx.lineWidth = 0.5;
      ctx.strokeRect(x + 0.25, y + 0.25, cellWidth - 0.5, cellHeight - 0.5);
    }
  }

  const footerY = canvas.height - 44;
  centeredText(
    `${MONTH_NAMES[state.now.getMonth()]} ${state.now.getDate()}, ${state.now.getFullYear()}`,
    footerY,
    14,
    '"Courier New"',
    "400",
    "#595246"
  );
}

function render() {
  const state = getState();
  const titleMode = state.mode === "calendar" ? "Calendar" : "Life Grid";
  const titleVariant = controls.variant.options[controls.variant.selectedIndex].text;
  controls.previewTitle.textContent = `${titleMode} / ${titleVariant}`;
  controls.previewMeta.textContent = `480 x 800 • ${state.mode === "calendar" ? "date view" : "life view"}`;

  const calendarMode = state.mode === "calendar";
  controls.weekStart.disabled = !calendarMode;
  controls.birthDate.disabled = calendarMode;
  controls.lifespan.disabled = calendarMode;
  controls.gridShape.disabled = calendarMode;
  controls.accentStyle.disabled = calendarMode;

  if (state.mode === "calendar") {
    if (state.variant === "quiet") {
      drawQuietCalendar(state);
    } else if (state.variant === "poster") {
      drawPosterCalendar(state);
    } else {
      drawFirmwareCalendar(state);
    }
  } else {
    drawLifeGrid(state, state.variant);
  }

  variantChips.forEach((chip) => {
    chip.classList.toggle("is-active", chip.dataset.variant === state.variant);
  });
}

function syncVariantControls(nextVariant) {
  controls.variant.value = nextVariant;
  render();
}

function downloadPreview() {
  const state = getState();
  const stamp = `${state.mode}-${state.variant}-${controls.date.value}`;
  const link = document.createElement("a");
  link.href = canvas.toDataURL("image/png");
  link.download = `screensaver-preview-${stamp}.png`;
  link.click();
}

function setDefaults() {
  const today = new Date();
  controls.date.value = toInputDate(today);
  controls.birthDate.value = "1990-01-01";
}

setDefaults();
render();

Object.values(controls).forEach((control) => {
  if (control instanceof HTMLElement && ["INPUT", "SELECT"].includes(control.tagName)) {
    control.addEventListener("input", render);
    control.addEventListener("change", render);
  }
});

controls.todayButton.addEventListener("click", () => {
  controls.date.value = toInputDate(new Date());
  render();
});

controls.downloadButton.addEventListener("click", downloadPreview);

variantChips.forEach((chip) => {
  chip.addEventListener("click", () => syncVariantControls(chip.dataset.variant));
});
