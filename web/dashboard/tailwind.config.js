/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./src/**/*.{js,jsx}', './public/index.html'],
  theme: {
    extend: {
      colors: {
        'slot-empty': '#374151',
        'slot-traveling': '#D97706',
        'slot-at-dest': '#059669',
        'slot-returning': '#2563EB',
        'slot-search': '#7C3AED',
        'slot-cooldown': '#6B7280',
      },
    },
  },
  plugins: [],
};
