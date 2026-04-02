const fs = require('fs');

const files = [
  'index.html',
  'src/components/layout/Navbar.jsx',
  'src/components/layout/Footer.jsx',
  'src/pages/Home.jsx',
  'src/pages/About.jsx',
  'src/index.css'
];

files.forEach(file => {
  if (!fs.existsSync(file)) return;
  let content = fs.readFileSync(file, 'utf8');
  
  // Name replacements
  content = content.replace(/QuantumPhotonics\./g, 'VitalQ.');
  content = content.replace(/Quantum Photonics/gi, 'Vitalquant');
  content = content.replace(/quantumphotonics\.test/g, 'vitalquant.com');
  content = content.replace(/Quantum precision/gi, 'VitalQ precision');
  content = content.replace(/quantum leap/gi, 'VitalQ leap');
  
  // Font replacements - targeting large texts
  content = content.replace(/hero-title/g, 'hero-title font-rx100');
  content = content.replace(/section-title/g, 'section-title font-rx100');
  content = content.replace(/text-5xl font-extrabold/g, 'text-5xl font-extrabold font-rx100');
  
  // For About page header (which has text-5xl lg:text-6xl font-bold)
  content = content.replace(/text-5xl lg:text-6xl font-bold/g, 'text-5xl lg:text-6xl font-bold font-rx100');
  
  fs.writeFileSync(file, content);
});

// Update index.css to include RX100
let css = fs.readFileSync('src/index.css', 'utf8');
if (!css.includes('.font-rx100')) {
  css += `\n\n@font-face {\n  font-family: 'RX100';\n  src: local('RX100'), url('https://fonts.cdnfonts.com/s/rx100/RX100.woff') format('woff');\n}\n\n.font-rx100 {\n  font-family: 'RX100', 'Geist Variable', sans-serif !important;\n}\n`;
  fs.writeFileSync('src/index.css', css);
}

console.log("Replacements complete.");
