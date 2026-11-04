import React from 'react';

const MONTHS = ["Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand", "Second Seed", 'Midyear', "Sun's Height", 'Last Seed', 'Hearthfire', 'Frostfall', "Sun's Dusk", 'Evening Star'];
const DAYS = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];

export function formatGameTime(time, calendar) {
  if (!Number.isFinite(time) || time < 0 || !calendar || !Number.isFinite(calendar.time) || !Number.isFinite(calendar.hour)) return '';
  // Anchor to the running save's calendar, including custom-start dates. Skyrim has no leap years.
  // Display to the nearest minute so float32 clock drift cannot turn midnight into yesterday.
  const totalMinutes = Math.round((time - calendar.time + calendar.hour / 24) * 1440);
  const dayOffset = Math.floor(totalMinutes / 1440);
  let date = calendar.year * 365 + DAYS.slice(0, calendar.month).reduce((a, b) => a + b, 0)
    + Math.floor(calendar.day) - 1 + dayOffset;
  const year = Math.floor(date / 365);
  date -= year * 365;
  let month = 0;
  while (date >= DAYS[month]) date -= DAYS[month++];
  const minutes = totalMinutes - dayOffset * 1440;
  return `${date + 1} ${MONTHS[month]}, 4E ${year} ${String(Math.floor(minutes / 60)).padStart(2, '0')}:${String(minutes % 60).padStart(2, '0')}`;
}

export default function GameTime({ time, calendar }) {
  const label = formatGameTime(time, calendar);
  return label ? <div className="text-[10px] text-gray-500 mt-0.5">{label}</div> : null;
}
