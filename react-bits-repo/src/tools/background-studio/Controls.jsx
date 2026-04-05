import { Box, Flex, Text, Icon, Input, Code, Button, Tooltip } from '@chakra-ui/react';
import { ChevronDown, RotateCcw, Share2, Code2, Plus, X, ExternalLink, Info } from 'lucide-react';
import { TbCopy, TbCopyCheckFilled } from 'react-icons/tb';
import { useState, useRef, useEffect, useCallback, useMemo } from 'react';
import { Link } from 'react-router-dom';
import { BACKGROUNDS, getDocsPath } from './backgrounds';
import { generateExportCode } from './utils/exportCode';
import { useOptions } from '../../components/context/OptionsContext/useOptions';
import PreviewSliderVertical from '../../components/common/Preview/PreviewSliderVertical';
import PreviewSwitch from '../../components/common/Preview/PreviewSwitch';
import PreviewSelect from '../../components/common/Preview/PreviewSelect';
import CodeHighlighter from '../../components/code/CodeHighlighter';

const BackgroundSelector = ({ selectedId, onSelect }) => {
  const [isOpen, setIsOpen] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const dropdownRef = useRef(null);
  const searchInputRef = useRef(null);

  useEffect(() => {
    const handleClickOutside = e => {
      if (dropdownRef.current && !dropdownRef.current.contains(e.target)) {
        setIsOpen(false);
        setSearchQuery('');
      }
    };
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  useEffect(() => {
    if (isOpen && searchInputRef.current) {
      searchInputRef.current.focus();
    }
  }, [isOpen]);

  const selected = BACKGROUNDS.find(bg => bg.id === selectedId) || BACKGROUNDS[0];

  const filteredBackgrounds = useMemo(() => {
    const sorted = [...BACKGROUNDS].sort((a, b) => a.label.localeCompare(b.label));
    if (!searchQuery.trim()) return sorted;
    const query = searchQuery.toLowerCase();
    return sorted.filter(bg => bg.label.toLowerCase().includes(query));
  }, [searchQuery]);

  return (
    <Box position="relative" ref={dropdownRef} mb={4}>
      <Flex
        as="button"
        onClick={() => setIsOpen(!isOpen)}
        align="center"
        justify="space-between"
        w="100%"
        px={3}
        py={2.5}
        bg="#170D27"
        border="1px solid #271E37"
        borderRadius="12px"
        cursor="pointer"
        transition="all 0.2s"
        _hover={{ borderColor: '#392e4e' }}
      >
        <Text fontSize="14px" fontWeight={600} color="#fff">
          {selected.label}
        </Text>
        <Icon
          as={ChevronDown}
          boxSize={4}
          color="#988BC7"
          transition="transform 0.2s"
          transform={isOpen ? 'rotate(180deg)' : 'rotate(0deg)'}
        />
      </Flex>

      <Box
        position="absolute"
        top="100%"
        left={0}
        right={0}
        mt={2}
        bg="#0D0716"
        border="1px solid #271E37"
        borderRadius="12px"
        maxH="350px"
        opacity={isOpen ? 1 : 0}
        visibility={isOpen ? 'visible' : 'hidden'}
        transform={isOpen ? 'translateY(0)' : 'translateY(-8px)'}
        transition="all 0.2s cubic-bezier(0.4, 0, 0.2, 1)"
        zIndex={100}
        overflow="hidden"
      >
        <Box p={2} borderBottom="1px solid #271E37">
          <Input
            ref={searchInputRef}
            placeholder="Search backgrounds..."
            value={searchQuery}
            onChange={e => setSearchQuery(e.target.value)}
            size="sm"
            bg="#170D27"
            border="1px solid #271E37"
            borderRadius="8px"
            color="#fff"
            fontSize="13px"
            _placeholder={{ color: '#988BC7' }}
            _hover={{ borderColor: '#392e4e' }}
            _focus={{ borderColor: '#5227FF', boxShadow: 'none' }}
          />
        </Box>

        <Box
          maxH="280px"
          overflowY="auto"
          css={{
            '&::-webkit-scrollbar': { width: '6px' },
            '&::-webkit-scrollbar-track': { background: '#170D27' },
            '&::-webkit-scrollbar-thumb': { background: '#392e4e', borderRadius: '3px' }
          }}
        >
          {filteredBackgrounds.length === 0 ? (
            <Flex align="center" justify="center" py={4}>
              <Text fontSize="13px" color="#988BC7">
                No backgrounds found
              </Text>
            </Flex>
          ) : (
            filteredBackgrounds.map(bg => (
              <Flex
                key={bg.id}
                as="button"
                onClick={() => {
                  onSelect(bg.id);
                  setIsOpen(false);
                  setSearchQuery('');
                }}
                align="center"
                gap={2}
                w="100%"
                px={3}
                py={2.5}
                bg={selectedId === bg.id ? '#170D27' : 'transparent'}
                _hover={{ bg: '#170D27' }}
                transition="background 0.15s"
              >
                <Text fontSize="14px" fontWeight={500} color="#fff">
                  {bg.label}
                </Text>
              </Flex>
            ))
          )}
        </Box>
      </Box>
    </Box>
  );
};

const NumberControl = ({ prop, value, onChange }) => {
  const { name, label, min = 0, max = 100, step = 1 } = prop;

  return (
    <PreviewSliderVertical
      title={label}
      min={min}
      max={max}
      step={step}
      value={value}
      onChange={val => onChange(name, val)}
    />
  );
};

const BooleanControl = ({ prop, value, onChange }) => {
  const { name, label } = prop;

  return <PreviewSwitch title={label} isChecked={value} onChange={checked => onChange(name, checked)} />;
};

const ColorControl = ({ prop, value, onChange }) => {
  const { name, label } = prop;

  return (
    <Flex justify="space-between" align="center" my={6}>
      <Text fontSize="sm">{label}</Text>
      <Flex align="center" gap={2}>
        <Text fontSize="xs" color="#B19EEF" fontFamily="mono">
          {value}
        </Text>
        <Box position="relative">
          <Box w="28px" h="28px" borderRadius="6px" bg={value} border="2px solid #271E37" />
          <Input
            type="color"
            value={value}
            onChange={e => onChange(name, e.target.value)}
            position="absolute"
            top={0}
            left={0}
            w="100%"
            h="100%"
            opacity={0}
            cursor="pointer"
          />
        </Box>
      </Flex>
    </Flex>
  );
};

const ColorArrayControl = ({ prop, value, onChange }) => {
  const { name, label, minItems = 1, maxItems = 5 } = prop;
  const colors = Array.isArray(value) ? value : [value];

  const updateColor = (index, newColor) => {
    const newColors = [...colors];
    newColors[index] = newColor;
    onChange(name, newColors);
  };

  const addColor = () => {
    if (colors.length < maxItems) {
      onChange(name, [...colors, '#ffffff']);
    }
  };

  const removeColor = index => {
    if (colors.length > minItems) {
      onChange(
        name,
        colors.filter((_, i) => i !== index)
      );
    }
  };

  return (
    <Box my={6}>
      <Text fontSize="sm" mb={2}>
        {label}
      </Text>
      <Flex flexWrap="wrap" gap={2} px={1} pt={1}>
        {colors.map((color, index) => (
          <Box key={index} position="relative" w="32px" h="32px">
            <Box
              w="32px"
              h="32px"
              borderRadius="6px"
              bg={color}
              border="2px solid #271E37"
              overflow="hidden"
              position="relative"
            >
              <Input
                type="color"
                value={color}
                onChange={e => updateColor(index, e.target.value)}
                position="absolute"
                top={0}
                left={0}
                w="32px"
                h="32px"
                opacity={0}
                cursor="pointer"
              />
            </Box>
            {colors.length > minItems && (
              <Flex
                as="button"
                onClick={() => removeColor(index)}
                position="absolute"
                top="-6px"
                right="-6px"
                w="16px"
                h="16px"
                bg="#170D27"
                border="1px solid #271E37"
                borderRadius="50%"
                align="center"
                justify="center"
                cursor="pointer"
              >
                <Icon as={X} boxSize={2.5} color="#988BC7" />
              </Flex>
            )}
          </Box>
        ))}
        {colors.length < maxItems && (
          <Flex
            as="button"
            onClick={addColor}
            w="32px"
            h="32px"
            borderRadius="6px"
            border="2px dashed #392e4e"
            align="center"
            justify="center"
            cursor="pointer"
            _hover={{ borderColor: '#5227FF' }}
            transition="border-color 0.2s"
          >
            <Icon as={Plus} boxSize={4} color="#988BC7" />
          </Flex>
        )}
      </Flex>
    </Box>
  );
};

const SelectControl = ({ prop, value, onChange }) => {
  const { name, label, options = [] } = prop;
  const selectOptions = options.map(opt => (typeof opt === 'string' ? { value: opt, label: opt } : opt));

  return (
    <PreviewSelect
      title={label}
      options={selectOptions}
      value={value}
      onChange={val => onChange(name, val)}
      width={120}
    />
  );
};

const RgbArrayControl = ({ prop, value, onChange }) => {
  const { name, label } = prop;
  const rgb = Array.isArray(value) ? value : [0.5, 0.5, 0.5];

  const rgbToHex = (r, g, b) => {
    const toHex = v =>
      Math.round(Math.max(0, Math.min(1, v)) * 255)
        .toString(16)
        .padStart(2, '0');
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
  };

  const hexToRgb = hex => {
    const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    if (result) {
      return [parseInt(result[1], 16) / 255, parseInt(result[2], 16) / 255, parseInt(result[3], 16) / 255];
    }
    return [0.5, 0.5, 0.5];
  };

  const hexValue = rgbToHex(rgb[0], rgb[1], rgb[2]);

  return (
    <Flex justify="space-between" align="center" my={6}>
      <Text fontSize="sm">{label}</Text>
      <Flex align="center" gap={2}>
        <Text fontSize="xs" color="#B19EEF" fontFamily="mono">
          [{rgb.map(v => v.toFixed(2)).join(', ')}]
        </Text>
        <Box position="relative">
          <Box w="28px" h="28px" borderRadius="6px" bg={hexValue} border="2px solid #271E37" />
          <Input
            type="color"
            value={hexValue}
            onChange={e => onChange(name, hexToRgb(e.target.value))}
            position="absolute"
            top={0}
            left={0}
            w="100%"
            h="100%"
            opacity={0}
            cursor="pointer"
          />
        </Box>
      </Flex>
    </Flex>
  );
};

const TextControl = ({ prop, value, onChange }) => {
  const { name, label } = prop;

  return (
    <Box my={6}>
      <Text fontSize="sm" mb={2}>
        {label}
      </Text>
      <Input
        type="text"
        value={value || ''}
        onChange={e => onChange(name, e.target.value)}
        bg="#170D27"
        border="1px solid #271E37"
        borderRadius="8px"
        color="#fff"
        fontSize="13px"
        px={3}
        py={2}
        _hover={{ borderColor: '#392e4e' }}
        _focus={{ borderColor: '#5227FF', boxShadow: 'none' }}
        placeholder={`Enter ${label.toLowerCase()}...`}
      />
    </Box>
  );
};

const DynamicControl = ({ prop, value, onChange }) => {
  switch (prop.type) {
    case 'number':
      return <NumberControl prop={prop} value={value} onChange={onChange} />;
    case 'boolean':
      return <BooleanControl prop={prop} value={value} onChange={onChange} />;
    case 'color':
      return <ColorControl prop={prop} value={value} onChange={onChange} />;
    case 'colorArray':
      return <ColorArrayControl prop={prop} value={value} onChange={onChange} />;
    case 'select':
      return <SelectControl prop={prop} value={value} onChange={onChange} />;
    case 'rgbArray':
      return <RgbArrayControl prop={prop} value={value} onChange={onChange} />;
    case 'text':
      return <TextControl prop={prop} value={value} onChange={onChange} />;
    default:
      return null;
  }
};

const PKG_MANAGERS = ['pnpm', 'npm', 'yarn', 'bun'];

const ExportModal = ({ isOpen, onClose, background, props }) => {
  const { languagePreset, stylePreset } = useOptions() || {};
  const [copied, setCopied] = useState(null);
  const [pkg, setPkg] = useState('pnpm');
  const codeRef = useRef(null);

  const language = languagePreset || 'JS';
  const style = stylePreset === 'TW' ? 'Tailwind' : stylePreset || 'CSS';

  const { commands, jsxCode } = useMemo(
    () => generateExportCode(background, props, { language, style }),
    [background, props, language, style]
  );

  const currentCommand = useMemo(() => {
    if (!commands) return '';
    const key = pkg === 'npm' ? 'npx' : pkg;
    return commands.shadcn?.[key] || '';
  }, [commands, pkg]);

  const copyToClipboard = useCallback((text, type) => {
    navigator.clipboard.writeText(text);
    setCopied(type);
    setTimeout(() => setCopied(null), 2000);
  }, []);

  if (!isOpen) return null;

  return (
    <Box
      position="fixed"
      top={0}
      left={0}
      right={0}
      bottom={0}
      bg="rgba(0,0,0,0.8)"
      zIndex={1000}
      display="flex"
      alignItems="center"
      justifyContent="center"
      onClick={onClose}
    >
      <Box
        bg="#0D0716"
        border="1px solid #271E37"
        borderRadius="40px"
        p={6}
        maxW="700px"
        w="90%"
        maxH="80vh"
        overflowY="auto"
        onClick={e => e.stopPropagation()}
        css={{
          '&::-webkit-scrollbar': { display: 'none' },
          scrollbarWidth: 'none',
          msOverflowStyle: 'none'
        }}
      >
        <Flex justify="space-between" align="center" mb={6}>
          <Text fontSize="18px" fontWeight={700} color="#fff">
            Export Your Background
          </Text>
          <Flex
            as="button"
            onClick={onClose}
            w={8}
            h={8}
            align="center"
            justify="center"
            borderRadius="8px"
            _hover={{ bg: '#170D27' }}
            cursor="pointer"
          >
            <Icon as={X} boxSize={5} color="#988BC7" />
          </Flex>
        </Flex>

        <Box mb={6}>
          <Text className="demo-title" mb={3}>
            Step 1: Install via CLI
          </Text>
          <Box className="cli-install">
            <Box className="cli-install-section cli-mode">
              <Flex className="cli-row" gap={0} mb={0}>
                <div className="pkg-buttons" data-role="pkg-buttons">
                  {PKG_MANAGERS.map(m => (
                    <button key={m} className="cli-tool-tab" data-active={pkg === m} onClick={() => setPkg(m)}>
                      {m}
                    </button>
                  ))}
                </div>
              </Flex>
              <Box className="code-wrapper" position="relative">
                <Code ref={codeRef} whiteSpace="pre" w="100%" overflow="auto" display="block">
                  {currentCommand}
                </Code>
                <Button
                  position="absolute"
                  h={10}
                  top="50%"
                  transform="translateY(-50%)"
                  right=".6em"
                  borderRadius="12px"
                  fontWeight={500}
                  backgroundColor={copied === 'install' ? '#5227FF' : '#060010'}
                  border="1px solid #392e4e"
                  color={copied === 'install' ? 'black' : 'white'}
                  _hover={{ backgroundColor: copied === 'install' ? '#5227FF' : '#170D27' }}
                  _active={{ backgroundColor: '#5227FF' }}
                  transition="background-color 0.3s ease"
                  onClick={() => copyToClipboard(currentCommand, 'install')}
                  aria-label="Copy installation command"
                >
                  {copied === 'install' ? (
                    <Icon as={TbCopyCheckFilled} color="#fff" boxSize={4} />
                  ) : (
                    <Icon as={TbCopy} color="#fff" boxSize={4} />
                  )}
                </Button>
              </Box>
            </Box>
          </Box>
        </Box>

        <Box>
          <Text className="demo-title" mb={3}>
            Step 2: Copy Code
          </Text>
          <CodeHighlighter
            language="jsx"
            codeString={jsxCode}
            showLineNumbers={true}
            maxLines={20}
            snippetId="export-jsx"
          />
        </Box>
      </Box>
    </Box>
  );
};

export default function Controls({
  background,
  backgroundId,
  props,
  onPropChange,
  onBackgroundChange,
  onReset,
  toolSelector,
  isMobile = false,
  disabled = false,
  canvasBg = '#060010',
  onCanvasBgChange
}) {
  const [exportOpen, setExportOpen] = useState(false);
  const [shareStatus, setShareStatus] = useState(null);

  const handleShare = useCallback(() => {
    navigator.clipboard.writeText(window.location.href);
    setShareStatus('Copied!');
    setTimeout(() => setShareStatus(null), 2000);
  }, []);

  return (
    <Flex
      direction="column"
      h="100%"
      overflow="hidden"
      opacity={disabled ? 0.5 : 1}
      pointerEvents={disabled ? 'none' : 'auto'}
      transition="opacity 0.2s"
    >
      {toolSelector && !isMobile && (
        <Box mb={4} flexShrink={0}>
          {toolSelector}
        </Box>
      )}

      <Box
        flex={1}
        overflowY="auto"
        css={{
          '&::-webkit-scrollbar': { display: 'none' },
          scrollbarWidth: 'none',
          msOverflowStyle: 'none'
        }}
      >
        <Text fontSize="11px" color="#988BC7" fontWeight={600} mb={3} textTransform="uppercase" letterSpacing="0.5px">
          Background
        </Text>
        <BackgroundSelector selectedId={backgroundId} onSelect={onBackgroundChange} />

        {onCanvasBgChange && (
          <Flex justify="space-between" align="center" mb={4}>
            <Flex align="center" gap={1.5}>
              <Text fontSize="sm">Canvas BG</Text>
              <Tooltip.Root openDelay={200} closeDelay={100} positioning={{ placement: 'top', gutter: 8 }}>
                <Tooltip.Trigger asChild>
                  <Flex align="center" justify="center" cursor="help">
                    <Icon as={Info} boxSize={3.5} color="#988BC7" />
                  </Flex>
                </Tooltip.Trigger>
                <Tooltip.Positioner>
                  <Tooltip.Content
                    bg="#060010"
                    border="1px solid #271e37"
                    color="#B19EEF"
                    fontSize="12px"
                    fontWeight="500"
                    px={3}
                    py={2}
                    borderRadius="10px"
                    maxW="220px"
                    lineHeight="1.4"
                  >
                    Light backgrounds might not work well with some effects, and some effects might not have a
                    transparent background.
                  </Tooltip.Content>
                </Tooltip.Positioner>
              </Tooltip.Root>
            </Flex>
            <Flex align="center" gap={2}>
              <Text fontSize="xs" color="#B19EEF" fontFamily="mono">
                {canvasBg}
              </Text>
              <Box position="relative">
                <Box w="28px" h="28px" borderRadius="6px" bg={canvasBg} border="2px solid #271E37" />
                <Input
                  type="color"
                  value={canvasBg}
                  onChange={e => onCanvasBgChange(e.target.value)}
                  position="absolute"
                  top={0}
                  left={0}
                  w="100%"
                  h="100%"
                  opacity={0}
                  cursor="pointer"
                />
              </Box>
            </Flex>
          </Flex>
        )}

        <Box h="1px" bg="#271E37" mb={4} />

        <Box>
          {background?.props?.length > 0 ? (
            background.props.map(prop => (
              <DynamicControl key={prop.name} prop={prop} value={props[prop.name]} onChange={onPropChange} />
            ))
          ) : (
            <Text fontSize="13px" color="#988BC7" textAlign="center" py={4}>
              No customizable props for this background
            </Text>
          )}
        </Box>
      </Box>

      <Box pt={4} borderTop="1px solid #271E37" mt={4} flexShrink={0}>
        <Flex gap={2} mb={2}>
          <Flex
            as="button"
            onClick={onReset}
            align="center"
            gap={1.5}
            px={3}
            py={2}
            bg="#170D27"
            border="1px solid #271E37"
            borderRadius="8px"
            cursor="pointer"
            flex={1}
            justify="center"
            _hover={{ borderColor: '#392e4e' }}
            transition="border-color 0.2s"
          >
            <Icon as={RotateCcw} boxSize={3.5} color="#988BC7" />
            <Text fontSize="12px" color="#988BC7" fontWeight={500}>
              Reset
            </Text>
          </Flex>
          <Flex
            as="button"
            onClick={handleShare}
            align="center"
            gap={1.5}
            px={3}
            py={2}
            bg="#170D27"
            border="1px solid #271E37"
            borderRadius="8px"
            cursor="pointer"
            flex={1}
            justify="center"
            _hover={{ borderColor: '#392e4e' }}
            transition="border-color 0.2s"
          >
            <Icon as={Share2} boxSize={3.5} color="#988BC7" />
            <Text fontSize="12px" color="#988BC7" fontWeight={500}>
              {shareStatus || 'Share'}
            </Text>
          </Flex>
        </Flex>
        <Flex
          as={Link}
          to={getDocsPath(background)}
          target="_blank"
          align="center"
          gap={1.5}
          px={3}
          py={2}
          mb={2}
          bg="#170D27"
          border="1px solid #271E37"
          borderRadius="8px"
          cursor="pointer"
          w="100%"
          justify="center"
          _hover={{ borderColor: '#392e4e' }}
          transition="border-color 0.2s"
          textDecoration="none"
        >
          <Icon as={ExternalLink} boxSize={3.5} color="#988BC7" />
          <Text fontSize="12px" color="#988BC7" fontWeight={500}>
            Read Full Docs
          </Text>
        </Flex>
        <Flex
          as="button"
          onClick={() => setExportOpen(true)}
          align="center"
          gap={1.5}
          px={3}
          py={2.5}
          bg="#5227FF"
          borderRadius="8px"
          cursor="pointer"
          w="100%"
          justify="center"
          _hover={{ bg: '#6b3fff' }}
          transition="background 0.2s"
        >
          <Icon as={Code2} boxSize={4} color="#fff" />
          <Text fontSize="13px" color="#fff" fontWeight={600}>
            Export Code
          </Text>
        </Flex>
      </Box>

      <ExportModal isOpen={exportOpen} onClose={() => setExportOpen(false)} background={background} props={props} />
    </Flex>
  );
}
