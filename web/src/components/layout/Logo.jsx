import React from 'react';

export default function Logo({ className = "w-10 h-10" }) {
  // Assuming the logo is saved as "/logo.png" (or similar) in your public folder.
  // Update the src path as needed to match where you saved the uploaded image.
  return (
    <div className={`overflow-hidden rounded-full flex items-center justify-center bg-white ${className}`}>
      <img 
        src="/logo.png" 
        alt="VitalQ Logo" 
        className="w-full h-full object-cover object-center"
      />
    </div>
  );
}